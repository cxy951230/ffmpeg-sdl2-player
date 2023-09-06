#include <stdio.h>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include <libavutil/imgutils.h>
#include "SDL2/SDL.h"
};
//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit=0;

int sfp_refresh_thread(void *opaque){
    SDL_Event event;
    event.type = SFM_REFRESH_EVENT;
    SDL_PushEvent(&event);
//    thread_exit=0;
//    while (!thread_exit) {
//        SDL_Event event;
//        event.type = SFM_REFRESH_EVENT;
//        SDL_PushEvent(&event);
//        SDL_Delay(40);
//    }
//    thread_exit=0;
//    //Break
//    SDL_Event event;
//    event.type = SFM_BREAK_EVENT;
//    SDL_PushEvent(&event);

    return 0;
}

int play_video(){
    AVFormatContext	*pFormatCtx;//媒体封装格式上下文
    int				i, videoindex;
    AVCodecContext	*pCodecCtx; //h264上下文
    AVCodecParameters *pCodecParameters;
    AVCodec			*pCodec;    //h264编码数据
    AVFrame	*pFrame,*pFrameYUV; //帧数据
    uint8_t *out_buffer;
    AVPacket *packet;
    int y_size;
    int ret, got_picture;
    struct SwsContext *img_convert_ctx;
    //�����ļ�·��
    char filepath[]="./Titanic.ts";

    //------------SDL----------------
    int screen_w,screen_h;
    SDL_Window *screen;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;
    SDL_Event event;

    int frame_cnt;
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    int res = avformat_open_input(&pFormatCtx,filepath,NULL,NULL);
    if(res!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
    if(avformat_find_stream_info(pFormatCtx,NULL)<0){
        printf("Couldn't find stream information.\n");
        return -1;
    }
    videoindex=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
            videoindex=i;
            break;
        }
    if(videoindex==-1){
        printf("Didn't find a video stream.\n");
        return -1;
    }

    pCodecParameters=pFormatCtx->streams[videoindex]->codecpar;
    pCodec=(AVCodec *)avcodec_find_decoder(pCodecParameters->codec_id);
    pCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(pCodec);
    if(pCodec==NULL){
        printf("Codec not found.\n");
        return -1;
    }
    if (avcodec_parameters_to_context(pCodecCtx, pCodecParameters) < 0) {
        printf("Failed to copy codec parameters to context.\n");
        return -1;
    }
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
        printf("Could not open codec.\n");
        return -1;
    }

    pFrame=av_frame_alloc();
    pFrameYUV=av_frame_alloc();
    //计算一帧画面的大小
    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    printf("size:%d\n",size);
    //每一帧输出的内存缓冲
    out_buffer=(uint8_t *)av_malloc(size);
    //把每一帧图像数据指针关联到pFrameYUV对象
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    //申请内存，存放每一帧h264数据
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    //Output Info-----------------------------
    printf("--------------- File Information ----------------\n");
    //打印视频流媒体信息，封装格式上下文，媒体流下标，输出文件，输出模式（非零值时表示输出文件，设置为零值表示输入文件）
    av_dump_format(pFormatCtx,0,filepath,0);
    printf("-------------------------------------------------\n");

    img_convert_ctx = sws_getCachedContext(NULL,pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                           pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL 2.0 Support for multiple windows
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,SDL_WINDOW_OPENGL);

    if(!screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);

    sdlRect.x=0;
    sdlRect.y=0;
    sdlRect.w=screen_w;
    sdlRect.h=screen_h;

    FILE *fp_h264 = fopen("test.h264", "wb+");
    FILE *fp_yuv = fopen("test.yuv", "wb+");
    frame_cnt=0;
    video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
    //读取一帧数据
    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if(event.type==SFM_REFRESH_EVENT){
            if(av_read_frame(pFormatCtx, packet)>=0){
                //判断数据的流下标是否是该媒体文件的视频流下标
                if(packet->stream_index==videoindex){
                    fwrite(packet->data, 1, packet->size, fp_h264);
                    //解码是否成功
                    if (avcodec_send_packet(pCodecCtx, packet) < 0) {
                        printf("Error sending a packet for decoding.\n");
                        break;
                    }
                    //解码数据送到pFrame
                    while (avcodec_receive_frame(pCodecCtx, pFrame) >= 0) {
                        // 在这里处理解码后的 pFrame
                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                                  pFrameYUV->data, pFrameYUV->linesize);
                        printf("Decoded frame index: %d\n", frame_cnt);
                        //yuv数据分别存在三个数组
//                        fwrite(pFrameYUV->data[0], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);
//                        fwrite(pFrameYUV->data[1], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);
//                        fwrite(pFrameYUV->data[2], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);

                        //sdl播放
                        SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
                        SDL_RenderClear( sdlRenderer );
                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
                        SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent( sdlRenderer );

                        // 处理完成后释放 pFrame
                        av_frame_unref(pFrame);
                        frame_cnt++;
                    }
                }
                av_packet_unref(packet);
                SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
            }else{
                //Exit Thread
                thread_exit=1;
            }
        }else if(event.type==SDL_QUIT){
            thread_exit=1;
        }else if(event.type==SFM_BREAK_EVENT){
            break;
        }
    }

    fclose(fp_h264);
    fclose(fp_yuv);
    sws_freeContext(img_convert_ctx);

    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
}

//#ifdef __cplusplus
//extern "C" {
//#endif
//
//static std::map<int,int> AUDIO_FORMAT_MAP = {
//        // AV_SAMPLE_FMT_NONE = -1,
//        {AV_SAMPLE_FMT_U8,  AUDIO_U8    },
//        {AV_SAMPLE_FMT_S16, AUDIO_S16SYS},
//        {AV_SAMPLE_FMT_S32, AUDIO_S32SYS},
//        {AV_SAMPLE_FMT_FLT, AUDIO_F32SYS}
//};


#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio    //48000 * (32/8)

unsigned int audioLen = 0;
unsigned char *audioChunk = NULL;
unsigned char *audioPos = NULL;

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
int play_audio(){
    AVFormatContext	*pFormatCtx;//媒体封装格式上下文
    int				i, audio_index;
    AVCodecContext	*pCodecCtx; //h264上下文
    AVCodecParameters *pCodecParameters;
    AVCodec			*pCodec;    //h264编码数据
    AVFrame	*pFrame,*pFrameYUV; //帧数据
    uint8_t *out_buffer;
//    AVPacket *packet;
    SDL_AudioSpec wanted_spec, spec;
    int y_size;
    int ret, got_picture;
    struct SwsContext *img_convert_ctx;
    //�����ļ�·��
    char filepath[]="/Users/chenxy/Desktop/output.mp4";

    //------------SDL----------------
//    int screen_w,screen_h;
//    SDL_Window *screen;
//    SDL_Renderer* sdlRenderer;
//    SDL_Texture* sdlTexture;
//    SDL_Rect sdlRect;
//    SDL_Thread *video_tid;
//    SDL_Event event;
//
//    int frame_cnt;
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    int res = avformat_open_input(&pFormatCtx,filepath,NULL,NULL);
    if(res!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
    av_dump_format(pFormatCtx,0,filepath,0);
    if(avformat_find_stream_info(pFormatCtx,NULL)<0){
        printf("Couldn't find stream information.\n");
        return -1;
    }
    audio_index =-1;
    for(i=0; i<pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){
            audio_index=i;
            break;
        }
    if(audio_index==-1){
        printf("Didn't find a audio stream.\n");
        return -1;
    }

    pCodecParameters=pFormatCtx->streams[audio_index]->codecpar;
    pCodec=(AVCodec *)avcodec_find_decoder(pCodecParameters->codec_id);
    pCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(pCodec);

    if (avcodec_parameters_to_context(pCodecCtx, pCodecParameters) < 0) {
        printf("Failed to copy codec parameters to context.\n");
        return -1;
    }
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
        printf("Could not open codec.\n");
        return -1;
    }

    int sampleRate = pCodecParameters->sample_rate;
    int channels = pCodecParameters->channels;
    printf("%d\n",pCodecParameters->format);

    // 重采样contex，先重采样成sdl支持的格式，再由sdl重采样播放
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;   //声音格式  SDL仅支持部分音频格式
    int out_sample_rate = /*48000; */ sampleRate;  //采样率
    int out_channels =    /*1;  */    channels;     //通道数
    int out_nb_samples = /*1024;  */  pCodecCtx->frame_size;
    printf("%d\n",out_nb_samples);
    //计算每一帧音频的大小，out_nb_samples采样数，不同编码mp3，aac取值不同
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);   //输出buff
    //缓冲区大小=最大帧（采样率48khz 位数32bit）数据大小*通道数
    unsigned char *outBuff = (unsigned char *)av_malloc(MAX_AUDIO_FRAME_SIZE * out_channels);
    uint64_t out_chn_layout =  av_get_default_channel_layout(channels); //AV_CH_LAYOUT_STEREO;  //通道布局 输出双声道

    SwrContext *au_convert_ctx = swr_alloc_set_opts(NULL,
                                                    out_chn_layout,                    /*out*/
                                                    out_sample_fmt,                    /*out*/
                                                    out_sample_rate,                   /*out*/
                                                    pCodecCtx->channel_layout,           /*in*/
                                                    pCodecCtx->sample_fmt,               /*in*/
                                                    pCodecCtx->sample_rate,              /*in*/
                                                    0,
                                                    NULL);

    swr_init(au_convert_ctx);


    ///   SDL
    if(SDL_Init(SDL_INIT_AUDIO)) {
        SDL_Log("init audio subsysytem failed.");
        return 0;
    }

    SDL_AudioSpec wantSpec;
    wantSpec.freq = out_sample_rate;
    // 和SwrContext的音频重采样参数保持一致
    wantSpec.format = AUDIO_S16SYS;//16位采样，所以送给callback的len是2*1024
    wantSpec.channels = out_channels;
    wantSpec.silence = 0;
    wantSpec.samples = out_nb_samples;
    wantSpec.callback = fill_audio;
    wantSpec.userdata = pCodecCtx;

    if(SDL_OpenAudio(&wantSpec, NULL) < 0) {
        printf("can not open SDL!\n");
        return -1;
    }

    SDL_PauseAudio(0);

    //3 解码
    AVPacket packet;
    pFrame = av_frame_alloc();
    while(av_read_frame(pFormatCtx, &packet)>=0){
        //判断数据的流下标是否是该媒体文件的视频流下标
        printf("11111111.\n");
        if(packet.stream_index==audio_index){
//                    fwrite(packet->data, 1, packet->size, fp_h264);
            printf("11111111.\n");
            //解码是否成功
            if (avcodec_send_packet(pCodecCtx, &packet) < 0) {
                printf("Error sending a packet for decoding.\n");
                break;
            }
            //解码数据送到pFrame
            while (avcodec_receive_frame(pCodecCtx, pFrame) >= 0) {
                // 在这里处理解码后的 pFrame
                swr_convert(au_convert_ctx, &outBuff, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pFrame->data, pFrame->nb_samples);
                while(audioLen > 0)//音频缓冲区中剩余的可填充数据长度
                    SDL_Delay(1);

                audioChunk = (unsigned char *)outBuff;//指向重采样后的音频数据的指针
                audioPos = audioChunk;//指向已填充数据的指针。
                audioLen = out_buffer_size;//需要填充的数据长度
                // 处理完成后释放 pFrame
                av_frame_unref(pFrame);
            }
        }
        av_packet_unref(&packet);
    }
    while(audioLen > 0)
        SDL_Delay(1);

    audioChunk = (unsigned char *)outBuff;
    audioPos = audioChunk;
    audioLen = out_buffer_size;
    return 0;


//
//    if(pCodec==NULL){
//        printf("Codec not found.\n");
//        return -1;
//    }
//    if (avcodec_parameters_to_context(pCodecCtx, pCodecParameters) < 0) {
//        printf("Failed to copy codec parameters to context.\n");
//        return -1;
//    }
//    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
//        printf("Could not open codec.\n");
//        return -1;
//    }
//
//    av_dump_format(pFormatCtx,0,filepath,0);
//
//    pFrame=av_frame_alloc();
//    pFrameYUV=av_frame_alloc();
//    //计算一帧画面的大小
//    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
//    printf("size:%d\n",size);
//    //每一帧输出的内存缓冲
//    out_buffer=(uint8_t *)av_malloc(size);
//    //把每一帧图像数据指针关联到pFrameYUV对象
//    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
//    //申请内存，存放每一帧h264数据
//    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
//    //Output Info-----------------------------
//    printf("--------------- File Information ----------------\n");
//    //打印视频流媒体信息，封装格式上下文，媒体流下标，输出文件，输出模式（非零值时表示输出文件，设置为零值表示输入文件）
//    av_dump_format(pFormatCtx,0,filepath,0);
//    printf("-------------------------------------------------\n");
//
//    img_convert_ctx = sws_getCachedContext(NULL,pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
//                                           pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
//    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
//        printf( "Could not initialize SDL - %s\n", SDL_GetError());
//        return -1;
//    }
//
//    //SDL 2.0 Support for multiple windows
//    screen_w = pCodecCtx->width;
//    screen_h = pCodecCtx->height;
//    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
//                              screen_w, screen_h,SDL_WINDOW_OPENGL);
//
//    if(!screen) {
//        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
//        return -1;
//    }
//    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
//    //IYUV: Y + U + V  (3 planes)
//    //YV12: Y + V + U  (3 planes)
//    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);
//
//    sdlRect.x=0;
//    sdlRect.y=0;
//    sdlRect.w=screen_w;
//    sdlRect.h=screen_h;
//
////    FILE *fp_h264 = fopen("test.h264", "wb+");
////    FILE *fp_yuv = fopen("test.yuv", "wb+");
//    frame_cnt=0;
//    video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
//    //读取一帧数据
//    for (;;) {
//        //Wait
//        SDL_WaitEvent(&event);
//        if(event.type==SFM_REFRESH_EVENT){
//            if(av_read_frame(pFormatCtx, packet)>=0){
//                //判断数据的流下标是否是该媒体文件的视频流下标
//                if(packet->stream_index==audio_index){
////                    fwrite(packet->data, 1, packet->size, fp_h264);
//
//                    //解码是否成功
//                    if (avcodec_send_packet(pCodecCtx, packet) < 0) {
//                        printf("Error sending a packet for decoding.\n");
//                        break;
//                    }
//                    //解码数据送到pFrame
//                    while (avcodec_receive_frame(pCodecCtx, pFrame) >= 0) {
//                        // 在这里处理解码后的 pFrame
//                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
//                                  pFrameYUV->data, pFrameYUV->linesize);
//                        printf("Decoded frame index: %d\n", frame_cnt);
//                        //yuv数据分别存在三个数组
////                        fwrite(pFrameYUV->data[0], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);
////                        fwrite(pFrameYUV->data[1], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);
////                        fwrite(pFrameYUV->data[2], 1, pCodecCtx->height*pCodecCtx->width, fp_yuv);
//
//                        //sdl播放
//                        SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
//                        SDL_RenderClear( sdlRenderer );
//                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
//                        SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
//                        SDL_RenderPresent( sdlRenderer );
//
//                        // 处理完成后释放 pFrame
//                        av_frame_unref(pFrame);
//                        frame_cnt++;
//                    }
//                }
//                av_packet_unref(packet);
//            }else{
//                //Exit Thread
//                thread_exit=1;
//            }
//        }else if(event.type==SDL_QUIT){
//            thread_exit=1;
//        }else if(event.type==SFM_BREAK_EVENT){
//            break;
//        }
//    }
//
////    fclose(fp_h264);
////    fclose(fp_yuv);
//    sws_freeContext(img_convert_ctx);
//
//    av_frame_free(&pFrameYUV);
//    av_frame_free(&pFrame);
//    avcodec_close(pCodecCtx);
//    avformat_close_input(&pFormatCtx);
}

int main(int argc, char* argv[])
{
    play_audio();
    return 0;
}
