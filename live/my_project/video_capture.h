#ifndef _VIDEO_CAPTURE_H
#define _VIDEO_CAPTURE_H

#include <linux/videodev2.h>
#include <pthread.h>

#define PIC_WIDTH 480
#define PIC_HEIGHT 288
#define BUF_SIZE PIC_WIDTH * PIC_HEIGHT * 2 * 2
//C270 YUV 4:2:2 frame size(char)

struct frame_O {
    unsigned char  data[546000] ;
    int size;        //有效
    pthread_rwlock_t rwlock;//读写锁
};


struct buffer {
    u_int8_t *start;
    u_int64_t length;
};

struct cam_data{

    unsigned char  cam_mbuf[BUF_SIZE] ;
    int wpos; 
    int rpos;
    pthread_cond_t captureOK;
    pthread_cond_t encodeOK;
    pthread_mutex_t lock;
                 };

struct camera {
    char *device_name;
    int fd;
    int width;
    int height;
    int display_depth;
    int image_size;
    int frame_number;
    struct v4l2_capability v4l2_cap;
    struct v4l2_cropcap v4l2_cropcap;
    struct v4l2_format v4l2_fmt;
    struct v4l2_crop crop;
    struct buffer *buffers;
};

void errno_exit(const char *s);

int xioctl(int fd, int request, void *arg);

void open_camera(struct camera *cam);
void close_camera(struct camera *cam);

void encode_frame(unsigned char *yuv_frame , int *wfd ,struct frame_O *frameout) ;

int buffOneFrame(struct cam_data *tmp, struct camera *cam) ;

int read_and_encode_frame(struct camera *cam);

void start_capturing(struct camera *cam);
void stop_capturing(struct camera *cam);

void init_camera(struct camera *cam);
void uninit_camera(struct camera *cam);

void init_mmap(struct camera *cam);

void v4l2_init(struct camera *cam);
void v4l2_close(struct camera *cam);

#endif
