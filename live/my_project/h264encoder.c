#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h264encoder.h"



void compress_begin(Encoder *en, int width, int height) {
    
    int m_frameRate=25;//帧率
    int m_bitRate=1000;//码率
    en->param = (x264_param_t *) malloc(sizeof(x264_param_t));
    en->picture = (x264_picture_t *) malloc(sizeof(x264_picture_t));
    x264_param_default_preset(en->param, "ultrafast" , "zerolatency" ); //傻瓜式配置参数，选择最优速度+实时编码模式


    en->param->i_width = width;//设置画面宽度
    en->param->i_height = height;//设置画面高度
         en->param->b_repeat_headers = 1;  // 重复SPS/PPS 放到关键帧前面         
         en->param->b_cabac = 1;         
         en->param->i_threads = 1;           
         en->param->i_fps_num = (int)m_frameRate;     
         en->param->i_fps_den = 1;
         en->param->i_keyint_max = 1;
         en->param->i_log_level = X264_LOG_NONE; //不显示encoder的信息

    if ((en->handle = x264_encoder_open(en->param)) == 0) {
        return;
    }

    x264_picture_alloc(en->picture, X264_CSP_I420, en->param->i_width,
            en->param->i_height);
    en->picture->img.i_csp = X264_CSP_I420;
    en->picture->img.i_plane = 3;

}

int compress_frame(Encoder *en, int type, uint8_t *in, uint8_t *out) {
    x264_picture_t pic_out;
    int nNal = 0;
    int result = 0;
    int i = 0 , j = 0 ;
    uint8_t *p_out = out;
    en->nal=NULL;
    uint8_t *p422;

    char *y = en->picture->img.plane[0];
    char *u = en->picture->img.plane[1];
    char *v = en->picture->img.plane[2];


///////////////////////////////////////YUV422转YUV420算法///////////////////////////////////////////
    int widthStep422 = en->param->i_width * 2;

    for(i = 0; i < en->param->i_height; i += 2)
    {
        p422 = in + i * widthStep422;

        for(j = 0; j < widthStep422; j+=4)
        {
            *(y++) = p422[j];
            *(u++) = p422[j+1];
            *(y++) = p422[j+2];
        }

        p422 += widthStep422;

        for(j = 0; j < widthStep422; j+=4)
        {
            *(y++) = p422[j];
            *(v++) = p422[j+3];
            *(y++) = p422[j+2];
        }
    }
//////////////////////////////////////////////////////////////////////////////////////

    switch (type) {
    case 0:
        en->picture->i_type = X264_TYPE_P;
        break;
    case 1:
        en->picture->i_type = X264_TYPE_IDR;
        break;
    case 2:
        en->picture->i_type = X264_TYPE_I;
        break;
    default:
        en->picture->i_type = X264_TYPE_AUTO;
        break;
    }

    if (x264_encoder_encode(en->handle, &(en->nal), &nNal, en->picture,
            &pic_out) < 0) {
        return -1;
    }
    en->picture->i_pts++;


    for (i = 0; i < nNal; i++) {
        memcpy(p_out, en->nal[i].p_payload, en->nal[i].i_payload);
        p_out += en->nal[i].i_payload;
        result += en->nal[i].i_payload;
    }

    return result;
}

void compress_end(Encoder *en) {
    if (en->picture) {
        x264_picture_clean(en->picture);
        free(en->picture);
        en->picture = 0;
    }
    if (en->param) {
        free(en->param);
        en->param = 0;
    }
    if (en->handle) {
        x264_encoder_close(en->handle);
    }
    free(en);
}
