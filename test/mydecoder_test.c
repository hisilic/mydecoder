#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/dict.h"
#include <libavutil/frame.h>

#include <unistd.h>
#include <sys/time.h>

#include "../mydecoder.h"

#define __DEBUG__ printf("%s : %d \n", __FUNCTION__, __LINE__);

static unsigned long long int current_ms()
{
    unsigned long long int res = 0;
    struct timeval tv;

    if (gettimeofday(&tv, NULL)) {
        return 0;
    }
    else {
        res = tv.tv_sec * 1000;
        res += tv.tv_usec / 1000;
    }
    return res;
}

int main(int argc, char *argv[])
{
    MyFrame frame;
    MyPacket packet;
    MyContext ctx = NULL;
    s32 video_stream_idx;
    s32 packet_size;
    s32 ret;
    s32 total_frame_num = 1000;
    s32 got_frame = 0;
    s32 frame_count = 0;
    u8 bgr_data[1920*1080*3];

    long long int start, end;
    float diff = 0.0;
    float fps = 0.0;

    packet = mydecoder_packet_alloc();
    if (!packet) {
        printf("Error allocating packet\n");
        exit(1);
    }

    frame = mydecoder_frame_alloc();
    if (!frame) {
        printf("Could not allocate video frame\n");
        exit(1);
    }
 
    ctx = mydecoder_context_alloc();
    mydecoder_open(&ctx, (const char *)argv[1], "h264_v4l2m2m", &total_frame_num);

    /* cannot get total frame number from *.h264, set to 1000 for test */
    if (total_frame_num == 0)
        total_frame_num = 1000;
    
    start = current_ms();

    while (1) {
        if (frame_count > total_frame_num)
            break;
        
        ret = mydecoder_get_packet(ctx, &packet, &packet_size);
        if (ret == AVERROR(EAGAIN)) 
            continue;

        if (packet_size) {
            mydecoder_decode(packet, &frame, &got_frame);
        }

        while (got_frame) {
            mydecoder_retrieve_frame(frame, bgr_data);
            got_frame--;
            frame_count++;
        }
    }

    end = current_ms();

    diff = (end - start) * 1.0 / (frame_count - 1);
    fps = (frame_count - 1) / ((end - start) / 1000.0);
    
    printf("frame_number: %d    time: %3.2fms    fps: %3.2f\n", frame_count - 1, diff, fps);

    mydecoder_close(frame, packet);
    
}

