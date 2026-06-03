#ifndef __I2C_MASTER_H
#define __I2C_MASTER_H

#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 初始化 I2C 主机
 * @param 
 * @return esp_err_t 错误码
 */
esp_err_t i2c_master_init(void);


#ifdef __cplusplus
}
#endif
#endif
