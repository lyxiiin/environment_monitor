#ifndef __SENSOR_PROTOCOL_H
#define __SENSOR_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  帧常量
 * ================================================================ */
#define SENSOR_PROTO_SYNC_HI    0xAA
#define SENSOR_PROTO_SYNC_LO    0x55
#define SENSOR_PROTO_MAX_FRAME  256     // 二进制帧最大字节数
#define SENSOR_PROTO_MAX_HEX    512     // 十六进制编码后最大字符数

/* ================================================================
 *  传感器类型枚举（可扩展）
 * ================================================================ */
typedef enum {
    SENSOR_TYPE_SCD41_CO2    = 0x01,    // SCD41 CO2 浓度 (uint16)
    SENSOR_TYPE_SCD41_TEMP   = 0x02,    // SCD41 温度 (float)
    SENSOR_TYPE_SCD41_HUM    = 0x03,    // SCD41 湿度 (float)
    SENSOR_TYPE_SHT40_TEMP   = 0x04,    // SHT40 温度 (float)
    SENSOR_TYPE_SHT40_HUM    = 0x05,    // SHT40 湿度 (float)
    /* 0x06 ~ 0xEF 预留给未来传感器 */
    SENSOR_TYPE_RESERVED_MAX = 0xEF,
    /* 0xF0 ~ 0xFF 扩展区 */
} sensor_type_t;

/* ================================================================
 *  TLV 记录结构
 * ================================================================ */
#define SENSOR_VALUE_MAX_LEN  8          // 单条 TLV 值的最大字节数

typedef struct {
    uint8_t type;                        // 传感器类型
    uint8_t len;                         // 值的字节数
    uint8_t value[SENSOR_VALUE_MAX_LEN]; // 值（大端序）
} sensor_tlv_t;

/* ================================================================
 *  帧构建器
 * ================================================================ */
#define SENSOR_FRAME_MAX_TLV  16         // 单帧最多 TLV 记录数

typedef struct {
    uint32_t  timestamp;                 // Unix 时间戳
    uint8_t   tlv_count;                 // 当前 TLV 数量
    sensor_tlv_t tlvs[SENSOR_FRAME_MAX_TLV];
} sensor_frame_t;

/* ================================================================
 *  公共 API
 * ================================================================ */

/**
 * @brief 初始化帧构建器，写入同步头和时间戳
 * @param frame 帧构建器指针
 */
void sensor_frame_init(sensor_frame_t *frame);

/**
 * @brief 向帧中追加一条 TLV 记录
 * @param frame  帧构建器指针
 * @param type   传感器类型
 * @param value  值数据指针
 * @param len    值数据的字节数（不超过 SENSOR_VALUE_MAX_LEN）
 * @return true  添加成功
 * @return false 参数无效或 TLV 已满
 */
bool sensor_frame_add_tlv(sensor_frame_t *frame, sensor_type_t type,
                          const uint8_t *value, uint8_t len);

/**
 * @brief 构建完整二进制帧（SYNC + 时间戳 + Count + TLV... + CRC8）
 * @param frame       帧构建器指针
 * @param out_bin     输出缓冲区（二进制）
 * @param out_bin_len 输出二进制数据长度
 * @return true  构建成功
 * @return false 缓冲区不足或帧为空
 */
bool sensor_frame_build(const sensor_frame_t *frame,
                        uint8_t *out_bin, uint8_t *out_bin_len);

/**
 * @brief 二进制数据转十六进制字符串（大写）
 * @param bin           输入二进制数据
 * @param bin_len       输入长度
 * @param hex_out       输出十六进制字符串缓冲区
 * @param hex_out_size  输出缓冲区大小（至少 bin_len*2+1）
 * @return true  转换成功
 * @return false 缓冲区不足
 */
bool sensor_frame_hex_encode(const uint8_t *bin, uint8_t bin_len,
                             char *hex_out, size_t hex_out_size);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_PROTOCOL_H */