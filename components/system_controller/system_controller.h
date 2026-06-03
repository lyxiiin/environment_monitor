#ifndef __SYSTEM_CONTROLLER_H
#define __SYSTEM_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动系统控制器，执行初始化并进入主循环
 * @note  此函数不会返回，内部包含无限主循环
 */
void system_controller_run(void);

#ifdef __cplusplus
}
#endif

#endif
