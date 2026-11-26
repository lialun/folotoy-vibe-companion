// Independent IMA ADPCM blocks: each header carries its predictor and step
// index. A missing block cannot corrupt the decoding of a later block. The
// encoder retains its index between blocks for continuity; each decoder uses
// only that block's header. Nibbles are packed low first. No heap is required.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADPCM_HEADER_BYTES   4      // [int16 LE predictor][uint8 index][uint8 rsv=0]
#define ADPCM_BLOCK_SAMPLES  1600   // 100ms @ 16kHz(= AUDIO_FRAME_BYTES / 2)
// 首样本走头部原值,其余 samples-1 个各占 4bit(奇数个时末字节高半字节填 0)
#define ADPCM_BLOCK_BYTES    (ADPCM_HEADER_BYTES + (ADPCM_BLOCK_SAMPLES / 2))   // 804

// 编码器自适应状态(4 字节,栈上即可;解码不需要跨块状态)
typedef struct {
    int16_t predictor;   // 上一次重建样本值
    uint8_t index;       // step_table 下标(0..88)
} adpcm_state_t;

// 复位到初始自适应状态。会话开始时调用(不跨会话携带上一次的静音自适应)。
void adpcm_state_reset(adpcm_state_t *st);

// 编码一个 block。samples 必须 ≥1;out_cap 不足或参数非法返回 0(不写越界)。
// 返回写入 out 的字节数 = ADPCM_HEADER_BYTES + samples/2(samples 为偶数时精确)。
// st 在返回时携带本块结束后的自适应 index,供下一块延续。
size_t adpcm_encode_block(adpcm_state_t *st, const int16_t *pcm, size_t samples,
                          uint8_t *out, size_t out_cap);

// 解码一个 block。samples = 该块原始样本数(协议固定:ADPCM_BLOCK_SAMPLES)。
// in_len / pcm_cap 不足或参数非法返回 0。返回写入 pcm 的样本数。
// 不需要外部状态:predictor/index 全部来自块头 —— 单块自洽。
size_t adpcm_decode_block(const uint8_t *in, size_t in_len, size_t samples,
                          int16_t *pcm, size_t pcm_cap);

#ifdef __cplusplus
}
#endif
