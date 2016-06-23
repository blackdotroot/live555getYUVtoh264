#include <sys/types.h>
#include  <sys/socket.h>
#include  <stdio.h>
#include  <netinet/in.h>
#include  <arpa/inet.h>
#include  <unistd.h>
#include  <string.h>
#include  <netdb.h>
#include  <sys/ioctl.h>
#include  <termios.h>
#include  <stdlib.h>
#include  <sys/stat.h>
#include  <fcntl.h>
#include  <signal.h>
#include  <sys/time.h>
#include  <stddef.h>
#include  <unistd.h>
#include  <time.h>
#include  <pthread.h>
#include "option.h"
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#include "H264_Live_Video_Stream.hh"
extern "C"
{
    #include "video_capture.h"
    #include "h264encoder.h"
}


struct cam_data Buff[1];//采集线程缓冲区 Buff[2]

pthread_t thread[3];//三个线程分别为：采集线程，编码线程，发布线程
int volatile flag[2];//状态标志位
int framelength = 0;//
struct frame_O tmp[1];//存储一帧数据
int pipefd[2];

struct camera *cam;

int init();
static void initBuff(struct cam_data *c);
static void initBuff1(struct frame_O *c);
void afterPlaying();
void play();
void *video_Capture_Thread(void*);
void *video_Encode_Thread(void*);
void *live555_trans_Thread(void*);
void thread_create(void);
void thread_wait(void);
static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
               char const* streamName, char const* inputFileName); // fwd

UsageEnvironment* env;
ByteStreamMemoryBufferSource* videoSource;
FramedSource* inputSource;
RTPSink* videoSink;
Boolean reuseFirstSource = true;//从内存中读取同一个数据源


#ifdef NOCONTROL
    int main(int argc, char** argv) {

        init();

        return 0;
    }
#endif


static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
               char const* streamName, char const* inputFileName) {
  char* url = rtspServer->rtspURL(sms);
  UsageEnvironment& env = rtspServer->envir();

  env << "Play this stream using the URL \"" << url << "\"\n";
  delete[] url;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////




int init() {

    cam = (struct camera *) malloc(sizeof(struct camera));

    if (!cam) {

        printf("malloc camera failure!\n");

        exit(1);

    }

    cam->device_name = (char*)CAM_NAME;

    cam->buffers = NULL;

    cam->width = PIC_WIDTH;

    cam->height = PIC_HEIGHT;

    framelength = YUV_FRAME_SIZE;

    v4l2_init(cam);
    // 初始化缓冲区
    {
    initBuff(Buff);

    initBuff1(tmp);    

    pipe(pipefd);//创建管道用于live555发送进程与编码进程的通信测试
    }
    thread_create();//创建线程
    thread_wait();//等待线程结束
    v4l2_close(cam);//关闭采集器和编码器
    return 0;

}

static void initBuff(struct cam_data *c) {
    c = (struct cam_data *) malloc(sizeof(struct cam_data));
    if (!c) {
        printf("malloc cam_data *c failure!\n");
        exit(1);
    }
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->captureOK, NULL);
    pthread_cond_init(&c->encodeOK, NULL);
    c->rpos = 0;
    c->wpos = 0;

}

static void initBuff1(struct frame_O *c) {

    c = (struct frame_O *) malloc(sizeof(struct frame_O*));
    if (!c) {
        printf("malloc frame_O *c failure!\n");
        exit(1);
    }
    c->size=0;

    pthread_rwlock_init(&c->rwlock, NULL);
}


void *video_Capture_Thread(void*) {

    int i = 0;

    int len = framelength;

    struct timeval now;

    struct timespec outtime;

    while (1) {

        usleep(DelayTime);

        gettimeofday(&now, NULL);

        outtime.tv_sec = now.tv_sec;

        outtime.tv_nsec = DelayTime * 1000;

        pthread_mutex_lock(&(Buff.lock));

        pthread_cond_timedwait(&(Buff.encodeOK), &(Buff.lock), &outtime);

        if (buffOneFrame(&Buff, cam)) {
            pthread_cond_signal(&(Buff.captureOK));
            pthread_mutex_unlock(&(Buff.lock));
        }
        pthread_cond_signal(&(Buff.captureOK));
        pthread_mutex_unlock(&(Buff.lock));

    }
    return 0;
}

void *video_Encode_Thread(void*) {

    int i = -1;

    while (1)

    {
        usleep(1);

        pthread_mutex_lock(&(Buff.lock));

        //编码一帧数据
        encode_frame((Buff.cam_mbuf), pipefd ,tmp);
        pthread_cond_signal(&(Buff.encodeOK));
        pthread_mutex_unlock(&(Buff.lock));

    }
    return 0;

}
void *live555_trans_Thread(void*){

    while(tmp!=NULL){
        sleep(1);

          // Begin by setting up our usage environment:
          TaskScheduler* scheduler = BasicTaskScheduler::createNew();
          env = BasicUsageEnvironment::createNew(*scheduler);

          UserAuthenticationDatabase* authDB = NULL;
        #ifdef ACCESS_CONTROL
          // To implement client access control to the RTSP server, do the following:
          authDB = new UserAuthenticationDatabase;
          authDB->addUserRecord("ls", "123"); // replace these with real strings
          // Repeat the above with each <username>, <password> that you wish to allow
          // access to the server.
        #endif

          // Create the RTSP server:
          RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554, authDB);
          if (rtspServer == NULL) {
            *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
            exit(1);
          }

          char const* descriptionString
            = "Session streamed by \"testOnDemandRTSPServer\"";


          // A H.264 video elementary stream:
          {
            char const* streamName = "live";
            char const* inputFileName = "01.264";
            ServerMediaSession* sms
              = ServerMediaSession::createNew(*env, streamName, streamName,
                              descriptionString);

            sms->addSubsession(H264LiveVideoServerMediaSubsession//使用我们自己的会话类
                       ::createNew(*env, reuseFirstSource, (struct frame_I *)tmp ,pipefd));

            rtspServer->addServerMediaSession(sms);

            announceStream(rtspServer, sms, streamName, inputFileName);
          }


        break;
    }

        //6/开启 doEventLoop ;
        env->taskScheduler().doEventLoop(); // does not return


    return 0;
}


void thread_create(void) {

    int temp;

    memset(&thread, 0, sizeof(thread));

    if ((temp = pthread_create(&thread[0], NULL, &video_Capture_Thread, NULL))
            != 0)

        printf("video_Capture_Thread create fail!\n");

    if ((temp = pthread_create(&thread[1], NULL, &video_Encode_Thread, NULL))
            != 0)

        printf("video_Encode_Thread create fail!\n");

    if ((temp = pthread_create(&thread[2], NULL, &live555_trans_Thread, NULL))
            != 0)

        printf("live555_trans_Thread create fail!\n");

}

void thread_wait(void) {
    if (thread[0] != 0) {

        pthread_join(thread[0], NULL);

    }
    if (thread[1] != 0) {

        pthread_join(thread[1], NULL);

    }
    if (thread[2] != 0) {

        pthread_join(thread[2], NULL);

    }

}
