#include <stdio.h>
#include <unistd.h>
#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavutil/avutil.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavdevice/avdevice.h>
#include "SDL2/SDL.h"
#include "screen.h"
#include <AudioToolbox/AudioToolbox.h>
};
//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit=0;
int read_pkg_flag=0;
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define SCREEN_FPS 30
AVFormatContext	*videoFormatCtx;//媒体封装格式上下文
//AVInputFormat *videoInputFmt;
int				i, videoindex, audioindex;
AVCodecContext	*videoCodecCtx; //h264上下文
AVCodecParameters *videoCodecParameters;
const AVCodec			*videoCodec;    //h264编码数据
AVFrame	*pFrame,*pFrameYUV; //帧数据
uint8_t *out_buffer;
AVPacket *packet;
AVDictionary *options;
struct SwsContext *img_convert_ctx;


//音频
AVCodecContext	*audioCodecCtx;
AVCodec			*audioCodec;
AVCodecParameters *audioCodecParameters;
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio    //48000 * (32/8)
//------------SDL----------------
int screen_w,screen_h;
SDL_Window *screen;
SDL_Renderer* sdlRenderer;
SDL_Texture* sdlTexture;
SDL_Rect sdlRect;
SDL_Thread *video_tid;
SDL_Event event;

//output
AVFormatContext	*outAVFormatContext;
const AVOutputFormat *output_format;
AVStream *video_st;
const AVCodec *outAVCodec;
AVCodecContext *outAVCodecContext;
AVCodecParameters *outCodecParameters;
//音频
AVStream *audio_stream;
const AVCodec *outAudioAVCodec;
AVCodecContext *outAudioAVCodecContext;
AVCodecParameters *outAudioCodecParameters;
int frame_cnt;
int frame_rate = 60;

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



int play_video(void *opaque){
//    video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
    int64_t currentTime = 0;
    int64_t last_pts = 0;
    for (;;) {
        if(thread_exit == 1 || read_pkg_flag == 1){
            break;
        }
        AVPacket *pkg = av_packet_alloc(); // 分配新的AVPacket内存
        if(av_read_frame(videoFormatCtx, pkg)>=0){
            // 处理视频
            if(pkg->stream_index == videoindex) {
                int64_t nowTime = av_gettime();

                double pts = (pkg->pts - last_pts) * av_q2d(videoFormatCtx->streams[videoindex]->time_base);
                last_pts = pkg->pts;
//                last_pts ==0 || nowTime - currentTime >= int(1000000 / frame_rate)
                if(pts > 0.016){
                    PacketNode *node = (PacketNode *)malloc(sizeof(PacketNode)); // 动态分配内存
                    node->packet = pkg;
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
//                    if(nowTime - currentTime - int(1000000 / frame_rate) > 0){
//                        currentTime = nowTime + (nowTime - currentTime - int(1000000 / frame_rate));
//                    } else{
//                        currentTime = nowTime;
//                    }
                } else{
                    continue;
                }
            }
//            // 处理音频
            if(pkg->stream_index == audioindex) {
                PacketNode *node = (PacketNode *)malloc(sizeof(PacketNode)); // 动态分配内存
                node->packet = pkg;
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
        }
    }

}

int play_audio(void *opaque){
    int sampleRate = audioCodecParameters->sample_rate;
    int channels = audioCodecParameters->channels;
//    printf("%d\n",pCodecParameters->format);

    // 重采样contex，先重采样成sdl支持的格式，再由sdl重采样播放
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;   //声音格式  SDL仅支持部分音频格式
    int out_sample_rate = /*48000; */ sampleRate;  //采样率
    int out_channels =    /*1;  */    channels;     //通道数
    int out_nb_samples = /*1024;  */  1024;
    printf("%d\n",out_nb_samples);
    //计算每一帧音频的大小，out_nb_samples采样数，不同编码mp3，aac取值不同
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);   //输出buff
    //缓冲区大小=最大帧（采样率48khz 位数32bit）数据大小*通道数
    unsigned char *outBuff = (unsigned char *)av_malloc(MAX_AUDIO_FRAME_SIZE * out_channels);
    uint64_t out_chn_layout =  av_get_default_channel_layout(channels); //AV_CH_LAYOUT_STEREO;  //通道布局 输出双声道
    SwrContext *au_convert_ctx = swr_alloc_set_opts(NULL,
                                                    outAudioAVCodecContext->channel_layout,                    /*out*/
                                                    outAudioAVCodecContext->sample_fmt,                    /*out*/
                                                    outAudioAVCodecContext->sample_rate,                   /*out*/
                                                    audioCodecCtx->channel_layout,           /*in*/
                                                    audioCodecCtx->sample_fmt,               /*in*/
                                                    audioCodecCtx->sample_rate,              /*in*/
                                                    0,
                                                    NULL);
//    int out_buffer_size2 = av_samples_get_buffer_size(NULL, 2, 512, AV_SAMPLE_FMT_FLT, 1);   //输出buff
    swr_init(au_convert_ctx);

    int fifo_size_1 = 0;


    int64_t current_pts = 0;
    int64_t pts_i = 0;
    int64_t cur_audio_clock = 0;
    for(;;){
        if(thread_exit == 1){
            break;
        }
        while (AUDIO_HEAD == NULL){
            SDL_Delay(10);
            continue;
        }
        AVPacket *packet = AUDIO_HEAD->packet;
        if (avcodec_send_packet(audioCodecCtx, packet) < 0) {
            printf("Error sending a packet for decoding.\n");
            break;
        }
        AVFrame *pFrame = av_frame_alloc();
        //解码数据送到pFrame
        while (avcodec_receive_frame(audioCodecCtx, pFrame) >= 0) {
            if (cur_audio_clock == 0){
                cur_audio_clock = pFrame->pts;
                pFrame->pts = 0;
            } else{
                pFrame->pts = (pFrame->pts - cur_audio_clock) * outAVCodecContext->time_base.den /1000000;
                printf(">>>>>>>>>>>.audio pts:%d\n", pFrame->pts);
                pFrame->pts = av_rescale_q(pFrame->pts, outAVCodecContext->time_base, audio_stream->time_base);
                printf(">>>>>222222>>>>>>real audio pts:%d\n", pFrame->pts);
            }
            double sec = pFrame->pts * av_q2d(videoFormatCtx->streams[audioindex]->time_base);
            printf("audio sec:%f\n",sec);
            int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);   //输出buff

            // 在这里处理解码后的 pFrame
//            swr_convert(au_convert_ctx, outData, 1024, (const uint8_t **)pFrame->data, pFrame->nb_samples);
//            //通道数总的采样点数量
            int data_size = pFrame->nb_samples * audioCodecParameters->channels;
            printf("data_size:%d\n",data_size);
//            for (int i = 0; i < pFrame->nb_samples; ++i) {
//                for (int ch = 0; ch < audioCodecCtx->channels; ++ch) {
//                    int16_t *sample = (int16_t *)(pFrame->data[ch] + i * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));
//                    *sample = (int16_t)(32767.0 * sin(2 * M_PI * 440.0 * (0 * pFrame->nb_samples + i) / 44100));
//                }
//            }
//            pFrame->pts=0;

            int len = swr_convert(au_convert_ctx, 0, 0,
                                  (const uint8_t **)pFrame->data, pFrame->nb_samples);

            fifo_size_1 = swr_get_out_samples(au_convert_ctx, 0);
            if (fifo_size_1 < 1024){
                continue;
            }
            AVFrame *pFrameFltp = av_frame_alloc();
            pFrameFltp->sample_rate = outAudioAVCodecContext->sample_rate;
            pFrameFltp->channel_layout = outAudioAVCodecContext->channel_layout;
            pFrameFltp->format = AV_SAMPLE_FMT_FLTP;
            pFrameFltp->channels = 2;
            pFrameFltp->nb_samples = 1024;
            pFrameFltp->pts = pFrame->pts;
//            pFrameFltp->pts = current_pts;
//            current_pts = current_pts + 1024;
            int linesize = av_samples_get_buffer_size(NULL, pFrameFltp->channels, pFrameFltp->nb_samples, AV_SAMPLE_FMT_FLTP, 1);

// 设置每个声道的 linesize
            for (int ch = 0; ch < pFrameFltp->channels; ch++) {
                pFrameFltp->linesize[ch] = linesize / pFrameFltp->channels;
            }
            int value = av_frame_get_buffer(pFrameFltp, 0);
            if (value<0) {
                printf("av_frame_get_buffer fail \n");
            }

            swr_convert(au_convert_ctx, pFrameFltp->data, pFrameFltp->nb_samples,
                        0, 0);

            if(avcodec_send_frame(outAudioAVCodecContext, pFrameFltp) <0){
                char errorBuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(AVERROR(errno), errorBuf, AV_ERROR_MAX_STRING_SIZE);
                fprintf(stderr, "Error sending frame for encoding: %s\n", errorBuf);
                // Handle error
            }


            AVPacket audioPacket;
            av_init_packet(&audioPacket);
            audioPacket.data = NULL;
            audioPacket.size = 0;
            while (1) {
                value = avcodec_receive_packet(outAudioAVCodecContext, &audioPacket);
                if (value==AVERROR(EAGAIN) || value == AVERROR_EOF) {
                    break;
                } else if (value<0) {
                    printf("avcodec_receive_packet fail \n");
                    break;
                }
                audioPacket.stream_index = audio_stream->index;
//                audioPacket.pts=current_pts;
//                current_pts = current_pts + 1;
                value = av_interleaved_write_frame(outAVFormatContext, &audioPacket);
                if (value<0) {
                    printf("av_interleaved_write_frame fail \n");
                    break;
                }
                av_packet_unref(&audioPacket);
            }
//            if (avcodec_receive_packet(outAudioAVCodecContext, &audioPacket) == 0) {
//                audioPacket.stream_index = audio_stream->index;
//                audioPacket.pts = current_pts;
//                audioPacket.dts = current_pts;
//                av_write_frame(outAVFormatContext, &audioPacket);
//                av_packet_unref(&audioPacket);
//            }

            //总采样数量 / 每秒采样数量 = 所需秒数
//            audio_clock += (double) data_size /(double) (audioCodecCtx->sample_rate);
//            printf("audio_clock:%f\n",audio_clock);
            // 处理完成后释放 pFrame
            av_frame_unref(pFrame);
//            current_pts += av_rescale_q(1, audio_stream->time_base, videoFormatCtx->streams[audioindex]->time_base);
        }
        av_packet_unref(packet);
        int value = 0;

        if (AUDIO_HEAD->next != NULL){
            AUDIO_HEAD = AUDIO_HEAD->next;
        } else{
            AUDIO_HEAD = NULL;
        }
    }
    return  0;
}

int write_video(void *opaque){

    for(;;){
        if(thread_exit == 1){
            break;
        }
        while (VIDEO_FRAME_HEAD == NULL){
            SDL_Delay(10);
            continue;
        }
        AVFrame *frame = VIDEO_FRAME_HEAD->frame;
        printf("pts:%d\n",frame->pts);

        int value = 0;
        AVPacket outPacket = {0};
        av_init_packet(&outPacket);
        outPacket.data = NULL;    // packet data will be allocated by the encoder
        outPacket.size = 0;
//                        AVPacket *outPacket = av_packet_alloc();
//                        av_init_packet(&outPacket);

//                        outAVCodecContext->time_base = pFrameYUV->time_base;
        value = avcodec_send_frame(outAVCodecContext, frame);
//                        pFrameYUV->format = outAVCodec->pix_fmts[0];
        if (value < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(value, errbuf, sizeof(errbuf));
            printf("Error opening codec: %s\n", errbuf);
            fprintf(stderr, "Error sending a frame for encoding\n");
            exit(1);
        }
        if(avcodec_receive_packet(outAVCodecContext, &outPacket)>=0){
            outPacket.stream_index = video_st->index;
//                            av_packet_rescale_ts(&outPacket, outAVCodecContext->time_base, video_st->time_base);
            if (av_write_frame(outAVFormatContext, &outPacket) != 0) {
                printf("\nerror in writing video frame");
            }
            av_packet_unref(&outPacket);
        }
        // 处理完成后释放 pFrame
        av_frame_free(&frame);
        av_packet_unref(&outPacket);
        if (VIDEO_FRAME_HEAD->next != NULL){
            VIDEO_FRAME_HEAD = VIDEO_FRAME_HEAD->next;
        } else{
            VIDEO_FRAME_HEAD = NULL;
        }
    }
    return  0;
}

void init_output_file(){
    outAVFormatContext = NULL;
    int value = 0;
    const char output_file[] = "./output.mp4";

    avformat_alloc_output_context2(&outAVFormatContext, NULL, NULL, output_file);
    if (!outAVFormatContext) {
        printf("\nerror in allocating av format output context");
        exit(1);
    }

    value = avio_open(&outAVFormatContext->pb, output_file, AVIO_FLAG_WRITE);
    if (value < 0) {
        printf("\nerror in avio_open");
        exit(1);
    }
    outAVCodec = avcodec_find_encoder(outAVFormatContext->oformat->video_codec);
//    outAVCodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    outAVCodecContext = (AVCodecContext  *)avcodec_alloc_context3(outAVCodec);
// 设置编码器上下文参数
//    outAVCodecContext->codec_id = AV_CODEC_ID_MPEG4;
    outAVCodecContext->width = videoCodecCtx->width;                          // 图片宽度/高度
    outAVCodecContext->height = videoCodecCtx->height;
    outAVCodecContext->pix_fmt = outAVCodec->pix_fmts[0];                         // 像素格式（这里通过编码器赋值，不需要自己指定）
//    outAVCodecContext->time_base = videoCodecCtx->time_base;
//    outAVCodecContext->framerate = videoCodecCtx->time_base;
    outAVCodecContext->time_base = (AVRational) {1, 100000};            //设置时间基，20为分母，1为分子，表示以1/20秒时间间隔播放一帧图像
    outAVCodecContext->framerate = (AVRational) {frame_rate, 1};
//    outAVCodecContext->bit_rate = 1000000;                                   // 目标的码率，即采样的码率；显然，采样码率越大，视频大小越大，画质越高
//    outAVCodecContext->gop_size = 12;                                        // I帧间隔(值越大，视频文件越小，编解码延时越长)
//    outAVCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
//    if(outAVCodecContext->codec_id == AV_CODEC_ID_H264)

    video_st = avformat_new_stream(outAVFormatContext, NULL);
//    video_st->codecpar->codec_id = outAVCodec->id;
//    video_st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
//    video_st->codecpar->width = videoCodecCtx->width;
//    video_st->codecpar->height = videoCodecCtx->height;
//    video_st->codecpar->format = AV_PIX_FMT_YUV420P;
    if (!video_st) {
        printf("\nerror in creating a av format new stream");
        exit(1);
    }

    /* set property of the video file */
    outCodecParameters = video_st->codecpar;
    value = avcodec_parameters_from_context(outCodecParameters,outAVCodecContext);
//    video_st->codecpar->format=AV_PIX_FMT_YUV420P;
    if (value < 0) {
        printf("\nerror in avcodec_parameters_from_context");
        exit(1);
    }

    value = avcodec_open2(outAVCodecContext, outAVCodec, NULL);
    if (value < 0) {
        printf("\nerror in opening the avcodec");
        exit(1);
    }

    //音频
    outAudioAVCodec = avcodec_find_encoder(outAVFormatContext->oformat->audio_codec);
    outAudioAVCodecContext = (AVCodecContext  *)avcodec_alloc_context3(outAudioAVCodec);
    outAudioAVCodecContext->sample_rate = 44100;
    outAudioAVCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
    outAudioAVCodecContext->channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    outAudioAVCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    outAudioAVCodecContext->bit_rate=128000;
    audio_stream = avformat_new_stream(outAVFormatContext, NULL);
    if (!audio_stream) {
        printf("\nerror in creating a audio av format new stream");
        exit(1);
    }
//    avcodec_parameters_copy(audio_stream->codecpar, audioCodecParameters);
//    audio_stream->codecpar->codec_tag = 0;
//    outAudioAVCodecContext->frame_size=MAX_AUDIO_FRAME_SIZE;
    outAudioCodecParameters = audio_stream->codecpar;
    value = avcodec_parameters_from_context(outAudioCodecParameters,outAudioAVCodecContext);
    outCodecParameters->frame_size=1024;
//    video_st->codecpar->format=AV_PIX_FMT_YUV420P;
    if (value < 0) {
        printf("\nerror in audio avcodec_parameters_from_context");
        exit(1);
    }

    value = avcodec_open2(outAudioAVCodecContext, outAudioAVCodec, NULL);
    if (value < 0) {
        printf("\nerror in opening the audio avcodec");
        exit(1);
    }

//    audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
//    audio_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
//    audio_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
//    audio_stream->codecpar->sample_rate = outAudioCodecParameters->sample_rate;;
//    audio_stream->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
//    audio_stream->codecpar->channels = 2;

    // Initialize audio codec
//    outAudioAVCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
//    if (!audioCodec) {
//        fprintf(stderr, "Error finding audio codec\n");
//        return -1;
//    }
//
//    AVCodecContext *audioCodecCtx = avcodec_alloc_context3(audioCodec);
//    if (!audioCodecCtx) {
//        fprintf(stderr, "Error allocating audio codec context\n");
//        return -1;
//    }
//
//    audioCodecCtx->sample_rate = AUDIO_SAMPLE_RATE;
//    audioCodecCtx->channel_layout = AUDIO_CHANNEL_LAYOUT;
//    audioCodecCtx->channels = av_get_channel_layout_nb_channels(AUDIO_CHANNEL_LAYOUT);
//    audioCodecCtx->sample_fmt = AUDIO_SAMPLE_FORMAT;
//
//    if (avcodec_open2(audioCodecCtx, audioCodec, NULL) < 0) {
//        fprintf(stderr, "Error opening audio codec\n");
//        return -1;
//    }
//
//    audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
//    audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;
//    audioStream->codecpar->format = audioCodecCtx->sample_fmt;
//    audioStream->codecpar->sample_rate = AUDIO_SAMPLE_RATE;
//    audioStream->codecpar->channel_layout = AUDIO_CHANNEL_LAYOUT;
//    audioStream->codecpar->channels = audioCodecCtx->channels;
//    avcodec_parameters_from_context(audioStream->codecpar, audioCodecCtx);



    value = avformat_write_header(outAVFormatContext, NULL);
    if (value < 0) {
        printf("\nerror in writing the header context");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    avformat_network_init();
    const AVInputFormat *videoInputFmt = NULL;
    const AVOutputFormat *audioOutputFmt = NULL;
    const char *videoDeviceName = "avfoundation"; // 你想要的设备名称

    // 使用设备名称查找输入格式
    while ((videoInputFmt = av_input_video_device_next(videoInputFmt))) {
        if(strcmp(videoInputFmt->name, videoDeviceName) == 0){
            printf("found avfoundation");
            break;
        }
    }

    int value = 0;

    videoFormatCtx = avformat_alloc_context();//Allocate an AVFormatContext.

    /* set frame per second */
    value = av_dict_set(&options, "framerate", "60", 0);
    if (value < 0) {
        printf("\nerror in setting dictionary value");
        exit(1);
    }

    value = av_dict_set(&options, "capture_cursor", "1", 0);
    if (value < 0) {
        printf("\nerror in setting capture_cursor values");
        exit(1);
    }

    value = av_dict_set(&options, "capture_mouse_clicks", "1", 0);
    if (value < 0) {
        printf("\nerror in setting capture_mouse_clicks values");
        exit(1);
    }

    value = av_dict_set(&options, "pixel_format", "yuyv422", 0);
    if (value < 0) {
        printf("\nerror in setting pixel_format values");
        exit(1);
    }

    value = avformat_open_input(&videoFormatCtx, "2:0", videoInputFmt, &options);
    if (value != 0) {
        printf("\nerror in opening input device");
        exit(1);
    }

    av_dump_format(videoFormatCtx,0,NULL,0);

//    AVFormatContext *audioFormatContext = NULL;
//    avformat_alloc_output_context2(&audioFormatContext, audioOutputFmt, NULL, NULL);
//    if (!audioFormatContext) {
//        printf("\nerror in allocating av format output context");
//        exit(1);
//    }
//    AVStream *audio_st;
//    audio_st = avformat_new_stream(audioFormatContext, NULL);


    videoindex = -1;

    audioindex = -1;

    /* find the first video stream index . Also there is an API available to do the below operations */
    for (int i = 0; i < videoFormatCtx->nb_streams; i++) // find video stream posistion/index.
    {
        if (videoFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            break;
        }

    }

    for (int i = 0; i < videoFormatCtx->nb_streams; i++) // find video stream posistion/index.
    {
        if (videoFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioindex = i;
            break;
        }

    }

    if (videoindex == -1) {
        printf("\nunable to find the video stream index. (-1)");
        exit(1);
    }

    if (audioindex == -1) {
        printf("\nunable to find the audio stream index. (-1)");
        exit(1);
    }

    //打开视频编码器
    videoCodecParameters = videoFormatCtx->streams[videoindex]->codecpar;

    videoCodec = (AVCodec *)avcodec_find_decoder(videoCodecParameters->codec_id);
    if (videoCodec == NULL) {
        printf("\nunable to find the decoder");
        exit(1);
    }
    videoCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecCtx, videoCodecParameters);
    value = avcodec_open2(videoCodecCtx, videoCodec, NULL);//Initialize the AVCodecContext to use the given AVCodec.
    if (value < 0) {
        printf("\nunable to open the av codec");
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(value, errbuf, sizeof(errbuf));
        printf("Error opening codec: %s\n", errbuf);
        exit(1);
    }

    //打开音频编码器
    audioCodecParameters=videoFormatCtx->streams[audioindex]->codecpar;
    audioCodec=(AVCodec *)avcodec_find_decoder(audioCodecParameters->codec_id);
    audioCodecCtx = (AVCodecContext  *)avcodec_alloc_context3(audioCodec);
    audioCodecCtx->frame_size=MAX_AUDIO_FRAME_SIZE;
    if (avcodec_parameters_to_context(audioCodecCtx, audioCodecParameters) < 0) {
        printf("Failed to copy codec parameters to context.\n");
        return -1;
    }
    if(avcodec_open2(audioCodecCtx, audioCodec,NULL)<0){
        printf("Could not open codec.\n");
        return -1;
    }



    //初始化输出formatCtx
    init_output_file();

    pFrame=av_frame_alloc();
//    pFrameYUV=av_frame_alloc();
    //计算一帧画面的大小
    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
    printf("size:%d\n",size);
    //每一帧输出的内存缓冲
//    out_buffer=(uint8_t *)av_malloc(size);
    //把每一帧图像数据指针关联到pFrameYUV对象
//    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
    //申请内存，存放每一帧h264数据
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));

    img_convert_ctx = sws_getCachedContext(NULL,videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt,
                                           videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
//    img_convert_ctx = sws_getContext(videoCodecCtx->width,
//                                     videoCodecCtx->height,
//                                     videoCodecCtx->pix_fmt,
//                             outAVCodecContext->width,
//                             outAVCodecContext->height,
//                             outAVCodecContext->pix_fmt,
//                             SWS_BICUBIC, NULL, NULL, NULL);
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL 2.0 Support for multiple windows
    screen_w = videoCodecCtx->width;
    screen_h = videoCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,SDL_WINDOW_OPENGL);

    if(!screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,videoCodecCtx->width,videoCodecCtx->height);

//    sdlRect.x=0;
//    sdlRect.y=0;
//    sdlRect.w=screen_w;
//    sdlRect.h=screen_h;

//    FILE *fp_h264 = fopen("test.h264", "wb+");
//    FILE *fp_yuv = fopen("test.yuv", "wb+");
    schedule_refresh(0);
    video_tid = SDL_CreateThread(play_video,NULL,NULL);
    SDL_CreateThread(write_video,NULL,NULL);
    SDL_CreateThread(play_audio,NULL,NULL);

//    FILE *fp_h264 = fopen("test1.h264", "wb+");
//    FILE *fp_yuv = fopen("test1.yuv", "wb+");

    frame_cnt=0;
    int packetCount=0;
    int64_t start_time = NULL;
    int64_t cur_clock = 0;
    //读取一帧数据
    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if(event.type==SFM_REFRESH_EVENT){
            int64_t begin = av_gettime();
            while (VIDEO_HEAD == NULL){
                SDL_Delay(10);
            }
            AVPacket *videoPacket = VIDEO_HEAD->packet;
            if (VIDEO_HEAD->next != NULL){
                VIDEO_HEAD = VIDEO_HEAD->next;
            } else{
                VIDEO_HEAD = NULL;
            }
            //解码是否成功
            if (avcodec_send_packet(videoCodecCtx, videoPacket) < 0) {
                printf("Error sending a packet for decoding.\n");
                break;
            }
//                    fwrite(packet->data, 1, packet->size, fp_h264);
            //解码数据送到pFrame
            int64_t currentTime = av_gettime();
            int64_t ptsaa = av_rescale_q(currentTime, videoFormatCtx->streams[videoindex]->time_base, AV_TIME_BASE_Q);

            while (avcodec_receive_frame(videoCodecCtx, pFrame) >= 0) {
                pFrameYUV=av_frame_alloc();
                //计算一帧画面的大小
                int yuv_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                //每一帧输出的内存缓冲
                uint8_t *yuv_buffer=(uint8_t *)av_malloc(yuv_size);
                //把每一帧图像数据指针关联到pFrameYUV对象
                av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, yuv_buffer, AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                //申请内存，存放每一帧h264数据

                // 在这里处理解码后的 pFrame
                pFrameYUV->width = pFrame->width;
                pFrameYUV->height = pFrame->height;
//                        av_frame_get_buffer(pFrameYUV, 0);
                pFrameYUV->format = outAVCodecContext->pix_fmt;
//                        pFrameYUV->pts = frame_cnt;
//                        pFrameYUV->pts = frame_cnt / 25 / av_q2d(video_st->time_base);

                if (cur_clock == 0){
                    pFrameYUV->pts = 0;
                    cur_clock = pFrame->pts;
                } else{
                    pFrameYUV->pts = (pFrame->pts - cur_clock) * outAVCodecContext->time_base.den /1000000;
                    pFrameYUV->pts = av_rescale_q(pFrameYUV->pts, outAVCodecContext->time_base, video_st->time_base);
                }

//                if (start_time == NULL){
//                    pFrameYUV->pts = 0;
//                    start_time = av_gettime();
//                }else{
//                    pFrameYUV->pts = (av_gettime() - start_time) * outAVCodecContext->time_base.den /1000000;
//                }
//                pFrameYUV->pts = av_rescale_q(pFrameYUV->pts, outAVCodecContext->time_base, video_st->time_base);

                sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, videoCodecCtx->height,
                          pFrameYUV->data, pFrameYUV->linesize);
//                printf("Decoded frame index: %d\n", frame_cnt);
                //yuv数据分别存在三个数组
//                        fwrite(pFrameYUV->data[0], 1, videoCodecCtx->height*videoCodecCtx->width, fp_yuv);
//                        fwrite(pFrameYUV->data[1], 1, videoCodecCtx->height*videoCodecCtx->width, fp_yuv);
//                        fwrite(pFrameYUV->data[2], 1, videoCodecCtx->height*videoCodecCtx->width, fp_yuv);
//                        pFrameYUV->width = pFrame->width;
//                        pFrameYUV->height = pFrame->height;
//                        av_frame_get_buffer(pFrameYUV, 3 * 8);
//                AVFrame *frame = (AVFrame *)av_malloc(sizeof(AVFrame));
//                av_frame_copy(frame, pFrameYUV);
//                AVFrame *frame = av_frame_alloc();
//                frame->format = pFrameYUV->format;  // 设置像素格式
//                frame->width = pFrameYUV->width;                // 设置宽度
//                frame->height = pFrameYUV->height;              // 设置高度
//                frame->pts = pFrameYUV->pts;
//                uint8_t *d0 = (uint8_t *)malloc(sizeof(pFrameYUV->data[0]));
//                memcpy(d0, pFrameYUV->data[0], sizeof(pFrameYUV->data[0]));
//                uint8_t *d1 = (uint8_t *)malloc(sizeof(pFrameYUV->data[1]));
//                memcpy(d1, pFrameYUV->data[1], sizeof(pFrameYUV->data[1]));
//                uint8_t *d2 = (uint8_t *)malloc(sizeof(pFrameYUV->data[2]));
//                memcpy(d2, pFrameYUV->data[2], sizeof(pFrameYUV->data[2]));
//                frame->data[0] = d0;
//                frame->data[1] = d1;
//                frame->data[2] = d2;
//                frame->linesize[0] = pFrameYUV->linesize[0];
//                frame->linesize[1] = pFrameYUV->linesize[1];
//                frame->linesize[2] = pFrameYUV->linesize[2];
//                uint8_t *d1[videoCodecCtx->height*videoCodecCtx->width];
//                uint8_t *d2[videoCodecCtx->height*videoCodecCtx->width];
//                uint8_t *d3[videoCodecCtx->height*videoCodecCtx->width];
////                for (int i = 0; i < sizeof(sourceArray) / sizeof(sourceArray[0]); i++) {
//                    d1[i] = pFrameYUV->data[0][i];
//                }
//                strcpy_array(d1, pFrameYUV->data[0], videoCodecCtx->height*videoCodecCtx->width);
//                value = av_frame_get_buffer(frame, 0);
//                if (value < 0) {
//                    // 缓冲区分配失败，错误处理
//                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
//                    av_strerror(value, errbuf, sizeof(errbuf));
//                    printf(">>>>>write: %s\n", errbuf);
//                    av_frame_free(&frame);
//                }
//                value = av_frame_copy(frame, pFrameYUV);
//                char errbuf[AV_ERROR_MAX_STRING_SIZE];
//                av_strerror(value, errbuf, sizeof(errbuf));
//                printf(">>>>>write: %s\n", errbuf);

                FrameNode *node = (FrameNode *)malloc(sizeof(FrameNode)); // 动态分配内存
                node->frame = pFrameYUV;
                node->next = NULL;

                if (VIDEO_FRAME_HEAD == NULL) {
                    VIDEO_FRAME_HEAD = node;
                } else {
                    FrameNode *n = VIDEO_FRAME_HEAD;
                    while (n->next != NULL) {
                        n = n->next;
                    }
                    n->next = node;
                }

                //sdl播放
                SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
                SDL_RenderClear( sdlRenderer );
                SDL_Rect srcRect = { 0, 0, pFrame->width, pFrame->height };
                SDL_Rect dstRect = { 0, 0, screen_w, screen_h };
                SDL_RenderCopy( sdlRenderer, sdlTexture, &srcRect, &dstRect );
                SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
                SDL_RenderPresent( sdlRenderer );
                double pts = pFrame->pts * av_q2d(videoFormatCtx->streams[videoindex]->time_base);
//                printf("pts:%d\n", pFrame->pts);
//                printf("timebase:%f\n", av_q2d(videoFormatCtx->streams[videoindex]->time_base));
//                printf("yuv pts:%f\n",pts);
                // 处理完成后释放 pFrame
                av_frame_unref(pFrame);
                frame_cnt++;
                int64_t end = av_gettime();
//                if((int)(1000 / frame_rate)- (int)(end - begin)/1000 > 0){
//                    schedule_refresh((int)(1000 / frame_rate)- (int)(end - begin)/1000);
//                }else{
//                    schedule_refresh(0);
//                }
//                schedule_refresh((int)(1000 / frame_rate));
                schedule_refresh(0);
            }
        }else if(event.type==SDL_QUIT){
            read_pkg_flag = 1;
            while (VIDEO_FRAME_HEAD != NULL || AUDIO_HEAD != NULL){
                SDL_Delay(10);
            }
            thread_exit=1;
            SDL_Delay(10);
            avcodec_send_frame(outAVCodecContext, NULL);
            while(true){
                AVPacket pkt;
                av_init_packet(&pkt);
                pkt.data = NULL;
                pkt.size = 0;
                if(avcodec_receive_packet(outAVCodecContext, &pkt) >= 0){
                    pkt.stream_index = video_st->index;
//                    av_packet_rescale_ts(&pkt, outAVCodecContext->time_base, video_st->time_base);
                    av_interleaved_write_frame(outAVFormatContext, &pkt);
                    av_packet_unref(&pkt);
                }else{
                    break;
                }
            }



            printf(">>>>sdl quit");
            value = av_write_trailer(outAVFormatContext);
            exit(1);
//            fclose(fp_yuv);
//            fclose(fp_h264);
//            char errbuf[AV_ERROR_MAX_STRING_SIZE];
//            av_strerror(value, errbuf, sizeof(errbuf));
//            printf(">>>>>write: %s\n", errbuf);

//            if (value < 0) {
//                printf("\nerror in writing av trailer");
//
//            }
            // 关闭输出文件
//            if (!(outAVFormatContext->oformat->flags & AVFMT_NOFILE)) {
//                avio_flush(outAVFormatContext->pb);
//                avio_close(outAVFormatContext->pb);
//            }

        }else if(event.type==SFM_BREAK_EVENT){
            printf(">>>>sdl SFM_BREAK_EVENT");
            break;
        }
    }

//    fclose(fp_h264);

    sws_freeContext(img_convert_ctx);

    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(videoCodecCtx);
    avformat_close_input(&videoFormatCtx);
}
