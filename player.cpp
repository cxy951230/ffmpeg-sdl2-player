#include <stdio.h>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include "SDL2/SDL.h"
#include "player.h"
};
AVCodecParameters *videoCodecParameters;
int screen_w,screen_h;
SDL_Window *screen;
SDL_Renderer* sdlRenderer;
SDL_Texture* sdlTexture;
SDL_Rect sdlRect;
SDL_Thread *video_tid;
SDL_Event event;

double frame_timer = (double) av_gettime() / 1000000.0;
double video_clock = 0;
double audio_clock = 0;
double time_base;

AVCodecContext	*videoCodecCtx; //h264上下文
AVCodec			*videoCodec;    //h264编码数据
AVCodecParameters *audioCodecParameters;
//音频
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio    //48000 * (32/8)

unsigned int audioLen = 0;
unsigned char *audioChunk = NULL;
unsigned char *audioPos = NULL;

AVCodecContext	*audioCodecCtx; //h264上下文
AVCodec			*audioCodec;    //h264编码数据

void fill_audio(void * udata, Uint8 * stream, int len)//len由重采样指定的samples决定
{
    SDL_memset(stream, 0, len);

    if(audioLen == 0)
        return;

    len = (len>audioLen ? audioLen : len);

    SDL_MixAudio(stream, audioPos, len, SDL_MIX_MAXVOLUME);//SDL_MIX_MAXVOLUME音量因子，控制音量0-128

    audioPos += len;
    audioLen -= len;
}

int play_audio(void *opaque){
    printf("",audioCodecCtx->time_base);
    int sampleRate = audioCodecParameters->sample_rate;
    int channels = audioCodecParameters->channels;
//    printf("%d\n",pCodecParameters->format);

    // 重采样contex，先重采样成sdl支持的格式，再由sdl重采样播放
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;   //声音格式  SDL仅支持部分音频格式
    int out_sample_rate = /*48000; */ sampleRate;  //采样率
    int out_channels =    /*1;  */    channels;     //通道数
    int out_nb_samples = /*1024;  */  1024;
    printf("%d\n",out_nb_samples);
    //计算每一帧音频的大小，out_nb_samples采样数，不同编码mp3，aac取值不同
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);   //输出buff
//    out_buffer_size = 8192;
//    out_buffer_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * out_nb_samples;
    //缓冲区大小=最大帧（采样率48khz 位数32bit）数据大小*通道数
    unsigned char *outBuff = (unsigned char *)av_malloc(MAX_AUDIO_FRAME_SIZE * out_channels);
//    uint8_t *outBuffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));
    uint64_t out_chn_layout =  av_get_default_channel_layout(channels); //AV_CH_LAYOUT_STEREO;  //通道布局 输出双声道

    SwrContext *au_convert_ctx = swr_alloc_set_opts(NULL,
                                                    out_chn_layout,                    /*out*/
                                                    out_sample_fmt,                    /*out*/
                                                    out_sample_rate,                   /*out*/
                                                    audioCodecCtx->channel_layout,           /*in*/
                                                    audioCodecCtx->sample_fmt,               /*in*/
                                                    audioCodecCtx->sample_rate,              /*in*/
                                                    0,
                                                    NULL);

    swr_init(au_convert_ctx);
    SDL_AudioSpec wantSpec;
    wantSpec.freq = sampleRate;
    // 和SwrContext的音频重采样参数保持一致
    wantSpec.format = AUDIO_S16SYS;//16位采样，所以送给callback的len是2*1024
    wantSpec.channels = channels;
    wantSpec.silence = 0;
    wantSpec.samples = 1024;
    wantSpec.callback = fill_audio;
    wantSpec.userdata = audioCodecCtx;

    if(SDL_OpenAudio(&wantSpec, NULL) < 0) {
        printf("can not open SDL!\n");
        return -1;
    }

    SDL_PauseAudio(0);

    for(;;){
        AVPacket *packet = AUDIO_HEAD->packet;
        if (AUDIO_HEAD->next != NULL){
            AUDIO_HEAD = AUDIO_HEAD->next;
        } else{
            AUDIO_HEAD = NULL;
        }
        if (avcodec_send_packet(audioCodecCtx, packet) < 0) {
            printf("Error sending a packet for decoding.\n");
            break;
        }
        AVFrame *pFrame = av_frame_alloc();
        //解码数据送到pFrame
        while (avcodec_receive_frame(audioCodecCtx, pFrame) >= 0) {
            // 在这里处理解码后的 pFrame
            swr_convert(au_convert_ctx, &outBuff, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pFrame->data, pFrame->nb_samples);
            while(audioLen > 0)//音频缓冲区中剩余的可填充数据长度
                SDL_Delay(1);
//            for (int i = 0; i < 1024; ++i) {
//                // 缩放和舍入操作，可以根据实际需求进行调整
//                outBuff[i] = (int16_t)(outBuff[i] * INT16_MAX);  // 缩放到 16 位整数范围
//            }
            audioChunk = (unsigned char *)outBuff;//指向重采样后的音频数据的指针
            audioPos = audioChunk;//指向已填充数据的指针。
            audioLen = out_buffer_size;//需要填充的数据长度
//            //这一帧的采样数据量
//            int audio_frame_size = pFrame->nb_samples * audioCodecParameters->channels * 2;
//            //该音频播放速度
//            double audio_speed = audioCodecCtx->sample_rate * 2 * audioCodecParameters->channels;
//            //这一帧播放完的时间
//            audio_clock += audio_frame_size / audio_speed;
            //通道数总的采样点数量
            int data_size = pFrame->nb_samples * audioCodecParameters->channels;
            printf("data_size:%d\n",data_size);
            //总采样数量 / 每秒采样数量 = 所需秒数
            audio_clock += (double) data_size /(double) (audioCodecCtx->sample_rate);
            printf("audio_clock:%f\n",audio_clock);
            // 处理完成后释放 pFrame
            av_frame_unref(pFrame);
        }
        av_packet_unref(packet);
    }
}

int play(void *opaque){
    AVFormatContext	*pFormatCtx;//媒体封装格式上下文
    int videoindex, audioindex;
//    AVCodecContext	*pCodecCtx; //h264上下文
//    AVCodec			*pCodec;    //h264编码数据
//    AVFrame	*pFrame,*pFrameYUV; //帧数据
//    uint8_t *out_buffer;
//    struct SwsContext *img_convert_ctx;
//    char filepath[]="/Users/chenxy/Desktop/1.mp4";
    char filepath[]="./output.mp4";

    avformat_network_init();
    pFormatCtx = avformat_alloc_context();
    int res = avformat_open_input(&pFormatCtx,filepath,NULL,NULL);
    if(res!=0){
        printf("Couldn't open input stream.\n");
        return 1;
    }
    if(avformat_find_stream_info(pFormatCtx,NULL)<0){
        printf("Couldn't find stream information.\n");
        return 1;
    }
    videoindex=-1;
    for(int i=0; i<pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            time_base = av_q2d(pFormatCtx->streams[i]->time_base);
            printf("frame_delay:%f\n",time_base);
            printf("duration:%d",pFormatCtx->streams[i]->duration);

            videoindex=i;
        }else if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){
            audioindex=i;
        }

    //打开视频编码器
    videoCodecParameters=pFormatCtx->streams[videoindex]->codecpar;
    videoCodec=(AVCodec *)avcodec_find_decoder(videoCodecParameters->codec_id);
    videoCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(videoCodec);
    if (avcodec_parameters_to_context(videoCodecCtx, videoCodecParameters) < 0) {
        printf("Failed to copy codec parameters to context.\n");
        return 1;
    }
    if(avcodec_open2(videoCodecCtx, videoCodec,NULL)<0){
        printf("Could not open codec.\n");
        return 1;
    }

    //打开音频编码器
    audioCodecParameters=pFormatCtx->streams[audioindex]->codecpar;
    audioCodec=(AVCodec *)avcodec_find_decoder(audioCodecParameters->codec_id);
    audioCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(audioCodec);

    if (avcodec_parameters_to_context(audioCodecCtx, audioCodecParameters) < 0) {
        printf("Failed to copy codec parameters to context.\n");
        return -1;
    }
    if(avcodec_open2(audioCodecCtx, audioCodec,NULL)<0){
        printf("Could not open codec.\n");
        return -1;
    }
    SDL_Thread *play_audio_thread = SDL_CreateThread(play_audio, "decode_video_thread", NULL);

    for (;;) {
        AVPacket *packet = av_packet_alloc(); // 分配新的AVPacket内存
        if(av_read_frame(pFormatCtx, packet)>=0){
            // 处理视频
            if(packet->stream_index == videoindex) {
                PacketNode *node = (PacketNode *)malloc(sizeof(PacketNode)); // 动态分配内存
                node->packet = packet;
                node->next = NULL;

                if (VIDEO_HEAD == NULL) {
                    VIDEO_HEAD = node;
                } else {
                    PacketNode *n = VIDEO_HEAD;
                    while (n->next != NULL) {
                        n = n->next;
                    }
                    n->next = node;
                }
            }
            // 处理音频
            if(packet->stream_index == audioindex) {
                PacketNode *node = (PacketNode *)malloc(sizeof(PacketNode)); // 动态分配内存
                node->packet = packet;
                node->next = NULL;

                if (AUDIO_HEAD == NULL) {
                    AUDIO_HEAD = node;
                } else {
                    PacketNode *n = AUDIO_HEAD;
                    while (n->next != NULL) {
                        n = n->next;
                    }
                    n->next = node;
                }
            }
        }else{
            break;
        }
    }

}
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)
//// 定时器回调函数，发送FF_REFRESH_EVENT事件，更新显示视频帧
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
    SDL_Event event;
    event.type = SFM_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

//// 设置定时器
static void schedule_refresh(int delay) {
    SDL_AddTimer(delay, sdl_refresh_timer_cb, NULL);
}
int64_t begin = av_gettime();
void video_refresh_timer(void *userdata){
    AVPacket *packet = VIDEO_HEAD->packet;
    if (VIDEO_HEAD->next != NULL){
        VIDEO_HEAD = VIDEO_HEAD->next;
    } else{
        VIDEO_HEAD = NULL;
    }




    //解码是否成功
//    int res = avcodec_send_packet(pCodecCtx, packet);
//    printf("%d\n",res);
    if (avcodec_send_packet(videoCodecCtx, packet) < 0) {
        printf("Error sending a packet for decoding.\n");
        return;
    }
    AVFrame	*pFrame,*pFrameYUV;
    uint8_t *out_buffer;
    struct SwsContext *img_convert_ctx;
    pFrame=av_frame_alloc();
    pFrameYUV=av_frame_alloc();
    //计算一帧画面的大小
    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
//    printf("size:%d\n",size);
    //每一帧输出的内存缓冲
    out_buffer=(uint8_t *)av_malloc(size);
    //把每一帧图像数据指针关联到pFrameYUV对象
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
    //申请内存，存放每一帧h264数据
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    //Output Info-----------------------------

    img_convert_ctx = sws_getCachedContext(NULL,videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt,
                                           videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

    if(screen == NULL){
        //SDL 2.0 Support for multiple windows
        screen_w = videoCodecCtx->width;
        screen_h = videoCodecCtx->height;
        screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  screen_w, screen_h,SDL_WINDOW_OPENGL);

        if(!screen) {
            printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
            return;
        }
        sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
        //IYUV: Y + U + V  (3 planes)
        //YV12: Y + V + U  (3 planes)
        sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,videoCodecCtx->width,videoCodecCtx->height);

    }


    //解码数据送到pFrame
//    res=avcodec_receive_frame(pCodecCtx, pFrame);
//    printf("%d\n",res);
    int delay = 0;
    while (avcodec_receive_frame(videoCodecCtx, pFrame) >= 0) {
        // 在这里处理解码后的 pFrame
        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, videoCodecCtx->height,
                  pFrameYUV->data, pFrameYUV->linesize);

        //sdl播放
        SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
        SDL_RenderClear( sdlRenderer );
        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
        SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent( sdlRenderer );
        //一帧视频所占用的时间
//        double frame_delay = av_q2d(videoCodecCtx->time_base);
        /* if we are repeating a frame, adjust clock accordingly */
        //该帧播放时间
//        double pts = pFrameYUV->pts * time_base;
//        printf("yuv pts:%f\n",pts);//
//        video_clock += pFrameYUV->repeat_pict * (time_base * 0.5);
//        video_clock += frame_delay;
//        printf("video_clock:%f\n",video_clock);
//        double audio_speed = audioCodecCtx->sample_rate * 2 * audioCodecParameters->channels;
//        double audio_speed = (double) (2*audioCodecParameters->channels * audioCodecCtx->sample_rate);
        //当前音频播放的时间戳
        double current_audio_clock = audio_clock - (audioLen / audioCodecCtx->sample_rate);

//        int64_t nowTime = av_gettime();
//        double sec = (nowTime - begin)/1000000;
//        printf("current_audio_clock:%f\n",current_audio_clock);
//        printf("sec:%f\n",sec);
        double pts = pFrame->pts * time_base;


        double diff =(pts - current_audio_clock) * 1000;
        printf("diff:%f\n",diff);
        double actual_delay = 0;
        if (diff > 0){//视频比音频快
            if(diff > 100){
                actual_delay = 0.1;
            } else{
                actual_delay = 0.010;
            }
        }
        /* Skip or repeat the frame. Take delay into account
           FFPlay still doesn't "know if this is the best guess." */
//        double sync_threshold = (delay > 0.01) ? delay : 0.01;
//        if (fabs(diff) < 10.0) {
//            if (diff <= -sync_threshold) {
//                delay = 0;
//            } else if (diff >= sync_threshold) {
//                delay = 2 * delay;
//            }
//        }
//
//        frame_timer += delay;
//        // 最终真正要延时的时间
//        double actual_delay = frame_timer - (av_gettime() / 1000000.0);
//        if (actual_delay < 0.010) {
//            // 延时时间过小就设置最小值
//            actual_delay = 0.010;
//        }
        printf("actual_delay:%f\n",actual_delay);

        schedule_refresh((int) (actual_delay * 1000));

        // 处理完成后释放 pFrame
        av_frame_unref(pFrame);

    }
    schedule_refresh(40);
    av_packet_unref(packet);
}


int main(int argc, char* argv[]){
    printf(">>>>>>>....");


    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    SDL_Thread *decode_video_thread = SDL_CreateThread(play, "play", NULL);


    sdlRect.x=0;
    sdlRect.y=0;
    sdlRect.w=screen_w;
    sdlRect.h=screen_h;
    schedule_refresh(40);
    for (;;) {
        // 等待SDL事件，否则阻塞
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_QUIT: // 退出
                goto Destroy;
            case SDL_KEYDOWN:
                break;
            case SFM_REFRESH_EVENT: // 定时器刷新事件
                video_refresh_timer(event.user.data1);

                break;
            default:
                break;
        }
    }

    Destroy:
    SDL_Quit();
    return 0;
}