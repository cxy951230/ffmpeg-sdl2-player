struct PacketNode{
    AVPacket *packet;
    PacketNode *pre;
    PacketNode *next;
};

struct FrameNode{
    AVFrame *frame;
    FrameNode *pre;
    FrameNode *next;
};
PacketNode *VIDEO_HEAD = NULL;
PacketNode *AUDIO_HEAD = NULL;
FrameNode *VIDEO_FRAME_HEAD = NULL;

