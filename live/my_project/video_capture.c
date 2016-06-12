#include <asm/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <linux/videodev2.h>
#include <dirent.h>
#include "video_capture.h"
#include "h264encoder.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

typedef unsigned char uint8_t;

//static char *dev_name = "/dev/video0";

char h264_file_name[100] = "01.264\0";
FILE *h264_fp;
uint8_t *h264_buf;


unsigned int n_buffers = 0;
DIR *dirp;
Encoder en;

int cnt = 0;

void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

int xioctl(int fd, int request, void *arg) {
    int r = 0;
    do {
        r = ioctl(fd, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

void open_camera(struct camera *cam) {
    struct stat st;

    if (-1 == stat(cam->device_name, &st)) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", cam->device_name,
                errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no device\n", cam->device_name);
        exit(EXIT_FAILURE);
    }

    cam->fd = open(cam->device_name, O_RDWR, 0); //  | O_NONBLOCK

    if (-1 == cam->fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", cam->device_name, errno,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void close_camera(struct camera *cam) {
    if (-1 == close(cam->fd))
        errno_exit("close");

    cam->fd = -1;
}

void init_file() {
    h264_fp = fopen(h264_file_name, "wa+");
}

void close_file() {
    fclose(h264_fp);
}

void init_encoder(struct camera *cam) {
    compress_begin(&en, cam->width, cam->height);
    h264_buf = (uint8_t *) malloc(
            sizeof(uint8_t) * cam->width * cam->height * 3); // 设置缓冲区

}

void close_encoder() {
    compress_end(&en);
    free(h264_buf);
}

void encode_frame(unsigned char *yuv_frame, int *wfd ,struct frame_O *frameout) {

    int h264_length = 0;
    int tt[2];
    static int count=0;
    h264_length = compress_frame(&en, -1, yuv_frame, h264_buf);

    if (h264_length > 0) {

        pthread_rwlock_wrlock(&(frameout->rwlock));//获取写入锁；

        memcpy(frameout->data, h264_buf, h264_length);

        frameout->size = h264_length;

        tt[0]=h264_length;

        tt[1]=count++;//count: max=2 147 483 647

        //向管道写入数据，通知发送线程。
            write(wfd[1], tt,8);

        pthread_rwlock_unlock(&(frameout->rwlock));//解锁

        }
}

int buffOneFrame(struct cam_data *tmp , struct camera *cam )
{
    unsigned char * data;

    int len;

    struct v4l2_buffer buf;

    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    //this operator below will change buf.index and (0 <= buf.index <= 3)
    if (-1 == ioctl(cam->fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
        case EAGAIN:
            return 0;
        case EIO:

        default:
            errno_exit("VIDIOC_DQBUF");
        }
    }
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


        data = (unsigned char *)(cam->buffers[buf.index].start);//当前帧的首地址

        len =(size_t)buf.bytesused;//当前帧的长度

        if(tmp->wpos+len<=BUF_SIZE)    //缓冲区剩余空间足够存放当前帧数据
        {
            memcpy(tmp->cam_mbuf+tmp->wpos, data ,len);//把一帧数据拷贝到缓冲区

            tmp->wpos+=len;
        }

        if (-1 == ioctl(cam->fd, VIDIOC_QBUF, &buf))//
            errno_exit("VIDIOC_QBUF");

        if(tmp->wpos+len>BUF_SIZE)    //缓冲区剩余空间不够存放当前帧数据，切换下一缓冲区
        {

            return 1;
        }


            return 0;


}


void start_capturing(struct camera *cam) {
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(cam->fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(cam->fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");

}

void stop_capturing(struct camera *cam) {
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(cam->fd, VIDIOC_STREAMOFF, &type))
        errno_exit("VIDIOC_STREAMOFF");

}
void uninit_camera(struct camera *cam) {
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
        if (-1 == munmap(cam->buffers[i].start, cam->buffers[i].length))
            errno_exit("munmap");

    free(cam->buffers);
}

void init_mmap(struct camera *cam) {
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    //分配内存
    if (-1 == xioctl(cam->fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support "
                    "memory mapping\n", cam->device_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", cam->device_name);
        exit(EXIT_FAILURE);
    }

    cam->buffers = calloc(req.count, sizeof(*(cam->buffers)));

    if (!cam->buffers) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        //将VIDIOC_REQBUFS中分配的数据缓存转换成物理地址
        if (-1 == xioctl(cam->fd, VIDIOC_QUERYBUF, &buf))
            errno_exit("VIDIOC_QUERYBUF");

        cam->buffers[n_buffers].length = buf.length;
        cam->buffers[n_buffers].start = mmap(NULL ,
                buf.length, PROT_READ | PROT_WRITE ,
                MAP_SHARED , cam->fd, buf.m.offset);

        if (MAP_FAILED == cam->buffers[n_buffers].start)
            errno_exit("mmap");
    }
}

void init_camera(struct camera *cam) {

    struct v4l2_format *fmt = &(cam->v4l2_fmt);

    CLEAR(*fmt);

    fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt->fmt.pix.width = cam->width;
    fmt->fmt.pix.height = cam->height;
    fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; //yuv422
    //  fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420
    fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == xioctl(cam->fd, VIDIOC_S_FMT, fmt))
        errno_exit("VIDIOC_S_FMT");


    init_mmap(cam);

}

void v4l2_init(struct camera *cam) {
    open_camera(cam);
    init_camera(cam);
    start_capturing(cam);
    init_encoder(cam);
    init_file();
}

void v4l2_close(struct camera *cam) {
    stop_capturing(cam);
    uninit_camera(cam);
    close_camera(cam);
    free(cam);
    close_file();
    close_encoder();
}
