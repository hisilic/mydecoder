#ifndef _MYDECODER_H_
#define _MYDECODER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_LOG 0

typedef unsigned char    u8;
typedef unsigned int     u32;
typedef char             s8;
typedef int              s32;
typedef void *           MyPacket;
typedef void *           MyFrame;
typedef void *           MyContext;

MyPacket mydecoder_packet_alloc(void);
MyFrame mydecoder_frame_alloc(void);
MyContext mydecoder_context_alloc(void);
s32 mydecoder_open(MyContext *ctx, const s8 *file_name, s8 *codec_name, s32 *frame_num);
s32 mydecoder_get_packet(MyContext ctx, MyPacket *packet, s32 *packet_size);
s32 mydecoder_decode(MyPacket packet, MyFrame frame, s32 *got_frame);
s32 mydecoder_retrieve_frame(MyFrame frame, u8 *bgr_data);
s32 mydecoder_close(MyFrame frame, MyPacket packet);

#ifdef __cplusplus
}
#endif
#endif
