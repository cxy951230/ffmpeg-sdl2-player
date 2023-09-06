#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include <libavutil/imgutils.h>
#include "SDL2/SDL.h"

struct AudioInfo{
    double pts;
    AVFrame *audioFrame;
};
struct PacketNode{
    AVPacket *packet;
    PacketNode *pre;
    PacketNode *next;

};
PacketNode *VIDEO_HEAD = NULL;
PacketNode *AUDIO_HEAD = NULL;

