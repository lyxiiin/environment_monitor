#include "sensor_protocol.h"
#include <string.h>
#include <time.h>

/* ================================================================
 *  CRC-8 计算（多项式 0x07，初始值 0x00）
 * ================================================================ */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ================================================================
 *  U32 写入大端序
 * ================================================================ */
static void write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

/* ================================================================
 *  U16 写入大端序
 * ================================================================ */
static void write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

/* ================================================================
 *  float 写入大端序（IEEE 754）
 * ================================================================ */
static void write_float_be(uint8_t *buf, float val)
{
    uint32_t raw;
    memcpy(&raw, &val, sizeof(raw));
    write_u32_be(buf, raw);
}

/* ================================================================
 *  公共 API 实现
 * ================================================================ */

void sensor_frame_init(sensor_frame_t *frame)
{
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    frame->timestamp = (uint32_t)time(NULL);
}

bool sensor_frame_add_tlv(sensor_frame_t *frame, sensor_type_t type,
                          const uint8_t *value, uint8_t len)
{
    if (!frame || !value) return false;
    if (len == 0 || len > SENSOR_VALUE_MAX_LEN) return false;
    if (frame->tlv_count >= SENSOR_FRAME_MAX_TLV) return false;

    sensor_tlv_t *tlv = &frame->tlvs[frame->tlv_count];
    tlv->type = (uint8_t)type;
    tlv->len  = len;
    memcpy(tlv->value, value, len);
    frame->tlv_count++;
    return true;
}

bool sensor_frame_build(const sensor_frame_t *frame,
                        uint8_t *out_bin, uint8_t *out_bin_len)
{
    if (!frame || !out_bin || !out_bin_len) return false;
    if (frame->tlv_count == 0) return false;

    /* 预估总长度：SYNC(2) + Timestamp(4) + Count(1) + TLV... + CRC(1) */
    uint16_t total = 2 + 4 + 1;
    for (uint8_t i = 0; i < frame->tlv_count; i++) {
        total += 1 + 1 + frame->tlvs[i].len;  // type + len + value
    }
    total += 1;  // CRC8

    if (total > SENSOR_PROTO_MAX_FRAME) return false;

    uint8_t *p = out_bin;

    /* 同步头 */
    *p++ = SENSOR_PROTO_SYNC_HI;
    *p++ = SENSOR_PROTO_SYNC_LO;

    /* 时间戳（4 字节大端） */
    write_u32_be(p, frame->timestamp);
    p += 4;

    /* TLV 数量 */
    *p++ = frame->tlv_count;

    /* TLV 记录 */
    for (uint8_t i = 0; i < frame->tlv_count; i++) {
        const sensor_tlv_t *tlv = &frame->tlvs[i];
        *p++ = tlv->type;
        *p++ = tlv->len;
        memcpy(p, tlv->value, tlv->len);
        p += tlv->len;
    }

    /* CRC8（校验 SYNC 之后、CRC 之前的所有字节） */
    uint8_t crc = crc8(out_bin + 2, (size_t)(p - out_bin - 2));
    *p++ = crc;

    *out_bin_len = (uint8_t)(p - out_bin);
    return true;
}

bool sensor_frame_hex_encode(const uint8_t *bin, uint8_t bin_len,
                             char *hex_out, size_t hex_out_size)
{
    if (!bin || !hex_out) return false;

    size_t needed = (size_t)bin_len * 2 + 1;  // +1 for null terminator
    if (hex_out_size < needed) return false;

    static const char hex_table[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < bin_len; i++) {
        hex_out[i * 2]     = hex_table[bin[i] >> 4];
        hex_out[i * 2 + 1] = hex_table[bin[i] & 0x0F];
    }
    hex_out[bin_len * 2] = '\0';
    return true;
}