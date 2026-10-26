/*
 * motor.h —— TB6612FNG ×2 驱动 4 路 TT 电机（带软启动 + 掉电保存的方向修正）
 *
 * TB6612 每片带 2 路，每路 3 个引脚：PWM / IN1 / IN2
 * STBY 在硬件上直接接 3V3（两片都接），不占 GPIO。
 *
 * 注意：ESP32 的 GPIO0 / 2 / 12 / 15 是启动模式脚，不要拿来驱动电机
 *
 * 用法（main.cpp 里）：
 *   motorBegin();  // setup
 *   motorSet(id, 120);  // 设定目标值，内部会自动软启动
 *   motorUpdate();      // loop 里每圈调用，推进斜坡
 */

#pragma once

#include <Arduino.h>
#include "config.h"

// 电机编号：0=左前 1=右前 2=左后 3=右后
enum MotorId : uint8_t {
  MOTOR_LF = 0,
  MOTOR_RF = 1,
  MOTOR_LR = 2,
  MOTOR_RR = 3,
  MOTOR_COUNT = WHEEL_COUNT,
};

// 引脚分配（已被占用的：16=手柄RX，21/22 留给以后的 OLED）
// 每路：PWM / IN1 / IN2
#define MOTOR_LF_PWM   25
#define MOTOR_LF_IN1   26
#define MOTOR_LF_IN2   27

#define MOTOR_RF_PWM   32
#define MOTOR_RF_IN1   33
#define MOTOR_RF_IN2   23

#define MOTOR_LR_PWM   18
#define MOTOR_LR_IN1   19
#define MOTOR_LR_IN2    5

#define MOTOR_RR_PWM   13
#define MOTOR_RR_IN1   14
#define MOTOR_RR_IN2    4

// PWM 频率 20kHz（超出人耳范围，电机不会啸叫）
#define MOTOR_PWM_FREQ 20000

// 斜坡每 5ms 走一步（200Hz），够平滑又不占 CPU
#define MOTOR_RAMP_TICK_MS 5

void motorBegin();

// 设定目标速度 -255~255（负数反转）。
// 实际输出会按斜坡慢慢靠过去，所以刚设定时轮子不会立刻满速。
void motorSet(int id, int speed);

// 全部目标清零（斜坡减速停）
void motorStopAll();

// 立刻断电停（急停、失控保护用，不减速）
void motorStopAllNow();

// loop 里每圈调用：推进斜坡，算出真正要写进 PWM 的值
void motorUpdate();

// 目标速度 / 当前实际输出（调试显示用）
int motorTarget(int id);
int motorOutput(int id);
