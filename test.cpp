#include <stdio.h>

#define __STDC_CONSTANT_MACROS
extern "C"
{
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include "SDL2/SDL.h"
};
OSStatus AudioUnitRenderCallbackFunction(void *inRefCon,
                                         AudioUnitRenderActionFlags *ioActionFlags,
                                         const AudioTimeStamp *inTimeStamp,
                                         UInt32 inBusNumber,
                                         UInt32 inNumberFrames,
                                         AudioBufferList *ioData) {
    // 获取 Audio Unit 实例
//    AudioUnit audioUnit = (AudioUnit)inRefCon;
//
//    // 遍历每个缓冲区
//    for (UInt32 bufferIndex = 0; bufferIndex < ioData->mNumberBuffers; ++bufferIndex) {
//        // 获取缓冲区
//        AudioBuffer *buffer = &ioData->mBuffers[bufferIndex];
//
//        // buffer->mData 指向 PCM 样本数据
//        // buffer->mDataByteSize 表示缓冲区中的数据字节数
//
//        // 在这里可以对 buffer->mData 中的音频数据进行处理
//        // 例如，可以将数据传递给 FFmpeg 进行编码等
//
//        // 注意：处理后的数据需要填充回 buffer->mData 中
//    }
// 获取 Audio Unit 实例
    AudioUnit outputAudioUnit = (AudioUnit)inRefCon;

    // 从扬声器读取音频数据
    OSStatus status = AudioUnitRender(outputAudioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, ioData);
    if (status != noErr) {
        // 处理错误
    }
    return noErr;
}
int play(void *opaque){
    printf(">>>>>>>>>>>>.start");
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    AudioUnit outputAudioUnit;
    AudioComponentInstanceNew(comp, &outputAudioUnit);

    AudioStreamBasicDescription audioFormat;
    audioFormat.mSampleRate = 44100.0;
    audioFormat.mFormatID = kAudioFormatLinearPCM;
    audioFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    audioFormat.mBytesPerPacket = 4;
    audioFormat.mFramesPerPacket = 1;
    audioFormat.mBytesPerFrame = 4;
    audioFormat.mChannelsPerFrame = 2;
    audioFormat.mBitsPerChannel = 32;

    AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &audioFormat, sizeof(audioFormat));

    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = AudioUnitRenderCallbackFunction;
    callbackStruct.inputProcRefCon = outputAudioUnit;

    AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &callbackStruct, sizeof(callbackStruct));
    AudioOutputUnitStart(outputAudioUnit);
}
int main() {
    SDL_Thread *decode_video_thread = SDL_CreateThread(play, "play", NULL);


    sleep(100);
    return 0;
}




//#include <iostream>
//#include <thread>
//
//using namespace std;
//
//extern "C" { //指定函数是c语言函数，函数名不包含重载标注
////引用ffmpeg头文件
//#include <libavformat/avformat.h>
//}
//
////预处理指令导入库
//#pragma comment(lib,"avformat.lib")
//#pragma comment(lib,"avutil.lib")
//#pragma comment(lib,"avcodec.lib")
//
//void PrintErr(int err)
//{
//    char buf[1024] = { 0 };
//    av_strerror(err, buf, sizeof(buf) - 1);
//    cerr << endl;
//}
//
//#define CERR(err) if(err!=0){ PrintErr(err);getchar();return -1;}
//
//int main(int argc, char* argv[])
//{
//    //打开媒体文件
//    const char* url = "/Users/chenxy/Desktop/1.mp4";
//
//    ////////////////////////////////////////////////////////////////////////////////////
//    /// 解封装
//    //解封装输入上下文
//    AVFormatContext* ic = nullptr;
//    auto re = avformat_open_input(&ic, url,
//                                  NULL,       //封装器格式 null 自动探测 根据后缀名或者文件头
//                                  NULL        //参数设置，rtsp需要设置
//    );
//    CERR(re);
//    //获取媒体信息 无头部格式
//    re = avformat_find_stream_info(ic, NULL);
//    CERR(re);
//
//    //打印封装信息
//    av_dump_format(ic, 0, url,
//                   0 //0表示上下文是输入 1 输出
//    );
//
//    AVStream* as = nullptr; //音频流
//    AVStream* vs = nullptr; //视频流
//    for (int i = 0; i < ic->nb_streams; i++)
//    {
//        //音频
//        if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
//        {
//            as = ic->streams[i];
//            cout << "=====音频=====" << endl;
//            cout << "sample_rate:" << as->codecpar->sample_rate << endl;
//        }
//        else if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
//        {
//            vs = ic->streams[i];
//            cout << "=========视频=========" << endl;
//            cout << "width:" << vs->codecpar->width << endl;
//            cout << "height:" << vs->codecpar->height << endl;
//        }
//    }
//    ////////////////////////////////////////////////////////////////////////////////////
//
//    ////////////////////////////////////////////////////////////////////////////////////
//    /// 解封装
//    //编码器上下文
//    const char* out_url = "/Users/chenxy/Desktop/2.mp4";
//    AVFormatContext* ec = nullptr;
//    re = avformat_alloc_output_context2(&ec, NULL, NULL,
//                                        out_url         //根据文件名推测封装格式
//    );
//    CERR(re);
//    //添加视频流、音频流
//    auto mvs = avformat_new_stream(ec, NULL);  //视频流
//    auto mas = avformat_new_stream(ec, NULL);  //音频流
//
//    //打开输出IO
//    re = avio_open(&ec->pb, out_url, AVIO_FLAG_WRITE);
//    CERR(re);
//
//
//    //设置编码音视频流参数
//    //ec->streams[0];
//    //mvs->codecpar;//视频参数
//    if (vs)
//    {
//        mvs->time_base = vs->time_base;// 时间基数与原视频一致
//        //从解封装复制参数
//        avcodec_parameters_copy(mvs->codecpar, vs->codecpar);
//    }
//
//    if (as)
//    {
//        mas->time_base = as->time_base;
//        //从解封装复制参数
//        avcodec_parameters_copy(mas->codecpar, as->codecpar);
//    }
//
//    //写入文件头
//    re = avformat_write_header(ec, NULL);
//    CERR(re);
//
//    //打印输出上下文
//    av_dump_format(ec, 0, out_url, 1);
//    ////////////////////////////////////////////////////////////////////////////////////
//
//
//    AVPacket pkt;
//    for (;;)
//    {
//        re = av_read_frame(ic, &pkt);
//        if (re != 0)
//        {
//            PrintErr(re);
//            break;
//        }
//
//        if (vs && pkt.stream_index == vs->index)
//        {
//            cout << "视频:";
//        }
//        else if (as && pkt.stream_index == as->index)
//        {
//            cout << "音频:";
//        }
//        cout << pkt.pts << " : " << pkt.dts << " :" << pkt.size << endl;
//        //写入音视频帧 会清理pkt
//        re = av_interleaved_write_frame(ec,&pkt);
//        if (re != 0)
//        {
//            PrintErr(re);
//        }
//        //av_packet_unref(&pkt);
//        //this_thread::sleep_for(100ms);
//    }
//
//    //写入结尾 包含文件偏移索引
//    re = av_write_trailer(ec);
//    if (re != 0)PrintErr(re);
//
//    avformat_close_input(&ic);
//
//    avio_closep(&ec->pb);
//    avformat_free_context(ec);
//    ec = nullptr;
//    return 0;
//}
