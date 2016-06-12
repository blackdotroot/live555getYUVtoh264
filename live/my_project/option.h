#ifndef OPTION_H
#define OPTION_H

#define PIC_WIDTH 640//画面宽度 320
#define PIC_HEIGHT 480//画面高度 240
#define YUV_FRAME_SIZE PIC_WIDTH * PIC_HEIGHT * 2//YUV422一帧数据的大小
#define BUF_SIZE       YUV_FRAME_SIZE * 4//缓冲区存储4帧数据
#define ENCODE_SIZE    (PIC_WIDTH * PIC_HEIGHT * 2)//编码之后的一帧缓存
#define CAM_NAME    "/dev/video0"//摄像头设备文件名
#define DelayTime 40*1000//(50us*1000=0.04s 25f/s)
#define NOCONTROL//不开启网络控制功能
//#define CONTROL//开启网络控制功能

#endif
