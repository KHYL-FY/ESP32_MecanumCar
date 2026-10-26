/*
 * battery.h —— 电池电压监测（分压 + ADC）
 *
 * 硬件接法（2S 锂电池，满电 8.4V）：
 *
 *   电池正极（开关之后、降压 IN+ 那个节点）
 *        │
 *        R1  (10k)
 *        │
 *        ├────── GPIO 34   （ADC1_CH6，只输引脚）
 *        │        └── 100nF 到 GND（抗噪，可选但推荐）
 *        R2  (4.7k)
 *        │
 *       GND
 *
 * 分压比 = R2 / (R1 + R2) = 4.7 / 14.7 ≈ 0.32
 *   8.4V → 2.69V   ✅ 在 3.3V 以内
 *   6.4V → 2.05V
 *
 * ⚠️ 绝对不要用 10k + 10k：8.4V 会变 4.2V，超过 ADC 上限，会损伤引脚。
 *
 * 为什么用 GPIO34：ESP32 的 ADC1 通道（32~39）不会被 WiFi/蓝牙抢占，
 * 34 是只输引脚，最安全。本项目没用到它。
 */

#pragma once

#include <Arduino.h>

#define BAT_ADC_PIN 34

// 电压等级
enum BatteryState : uint8_t {
  BAT_OK = 0,        // 正常
  BAT_LOW,           // 偏低（该准备充电了）
  BAT_CRITICAL,      // 危险（必须停车，否则过放损坏电池）
};

void batteryBegin();

// loop 里调用，内部限速（250ms 采一次，8 次平均）
void batteryPoll();

// 最近一次滤波后的电池电压（V）。未启用时返回 0
float batteryVolts();

// 单体电压（2S，除以 2）
float batteryCellVolts();

// 是否读到有效值（启用 + 有个合理读数）
bool batteryValid();

BatteryState batteryState();

// 文本标签（正常 / 低电 / 没电 / 关闭），给状态行用
const char *batteryLabel();
