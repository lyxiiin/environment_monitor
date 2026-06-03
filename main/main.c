/**
 * @file main.c
 * @brief 环境监测系统 — 启动入口
 *
 * 所有系统初始化、传感器采集、网络通信、主循环逻辑
 * 均已封装至 components/system_controller/ 组件中。
 * main.c 仅负责单行启动调用，保持极简。
 */
#include "system_controller.h"

void app_main(void)
{
    system_controller_run();
}
 