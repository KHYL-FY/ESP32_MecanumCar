/*
 * diag.h —— 运行时事件记录（用来事后诊断"卡一下"到底是链路还是电源）
 *
 * 为什么需要它：
 *   "电机卡一下"发生在几百毫秒内，人根本来不及看串口。
 *   这里把异常事件和时间戳记进一个环形缓冲，事后打一条命令就能回看：
 *     - 手柄链路断了多久（断连 → EMI / 接收器问题）
 *     - 当时的电池电压（低压 → 电池 / 保护板 / 降压欠压）
 *     - 是否触发过过放保护、急停
 *
 * 用法：串口按 g 打印并清空。
 */

#pragma once

#include <Arduino.h>

// 事件类型
enum DiagCode : uint8_t {
  DIAG_LINK_LOST = 1,   // 手柄数据超时（失控保护触发）
  DIAG_BAT_CRIT = 2,    // 电池过低，强制断电
  DIAG_ESTOP = 3,       // 串口急停
};

void diagBegin();

// 记录一次链路中断，durationMs = 断了多久
void diagLinkLost(uint32_t durationMs);

void diagBatteryCritical(float volts);

void diagEstop();

// 每圈调用：跟踪"行驶中的最低电压"，用来判断电池是不是被拉垮
void diagSampleBattery(float volts, bool armed);

// 打一条命令的入口：打印统计并清空
void diagPrint();
