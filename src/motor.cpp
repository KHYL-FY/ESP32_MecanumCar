/*
 * motor.cpp —— TB6612FNG 四路电机驱动实现
 *
 * 输出的三层处理（顺序不能乱）：
 *   1) 限幅到 cfg().maxDuty        —— 试车阶段限制最高速
 *   2) 死区补偿到 cfg().minDuty    —— 想动就至少给到能转起来的占空比
 *   3) 按 cfg().invert[] 翻转方向  —— 标定结果，掉电保存
 */

#include "motor.h"

namespace {

struct Channel {
  int pwm;
  int in1;
  int in2;
};

// 顺序必须和 MotorId 一致：LF, RF, LR, RR
const Channel kChannels[MOTOR_COUNT] = {
  { MOTOR_LF_PWM, MOTOR_LF_IN1, MOTOR_LF_IN2 },
  { MOTOR_RF_PWM, MOTOR_RF_IN1, MOTOR_RF_IN2 },
  { MOTOR_LR_PWM, MOTOR_LR_IN1, MOTOR_LR_IN2 },
  { MOTOR_RR_PWM, MOTOR_RR_IN1, MOTOR_RR_IN2 },
};

int g_target[MOTOR_COUNT] = {0, 0, 0, 0};  // 想要的速度
int g_out[MOTOR_COUNT]    = {0, 0, 0, 0};  // 当前速度（斜坡爬到这里）
uint32_t g_lastTickMs     = 0;

// 把"逻辑速度"落到引脚上
void applyOutput(int id, int speed) {
  const Channel &ch = kChannels[id];

  if (speed > 0) {
    digitalWrite(ch.in1, HIGH);
    digitalWrite(ch.in2, LOW);
  } else if (speed < 0) {
    digitalWrite(ch.in1, LOW);
    digitalWrite(ch.in2, HIGH);
  } else {
    digitalWrite(ch.in1, LOW);
    digitalWrite(ch.in2, LOW);
  }

  ledcWrite(ch.pwm, (uint8_t)abs(speed));
}

// 逻辑速度 -> 真实 PWM（限幅 + 死区补偿 + 方向翻转）
void output(int id, int logical) {
  int speed = logical;

  const int mx = cfg().maxDuty;
  if (speed > mx) speed = mx;
  if (speed < -mx) speed = -mx;

  const int mn = cfg().minDuty;
  if (speed > 0 && speed < mn) speed = mn;
  if (speed < 0 && speed > -mn) speed = -mn;

  if (cfg().invert[id]) speed = -speed;

  applyOutput(id, speed);
}

int clampSpeed(int v) {
  if (v > 255) return 255;
  if (v < -255) return -255;
  return v;
}

}  // namespace

void motorBegin() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    pinMode(kChannels[i].in1, OUTPUT);
    pinMode(kChannels[i].in2, OUTPUT);
    digitalWrite(kChannels[i].in1, LOW);
    digitalWrite(kChannels[i].in2, LOW);

    ledcAttach(kChannels[i].pwm, MOTOR_PWM_FREQ, 8);
    ledcWrite(kChannels[i].pwm, 0);
  }
  g_lastTickMs = millis();
  // 注意：STBY 在硬件上直接接 3V3，这里不控制
}

void motorSet(int id, int speed) {
  if (id < 0 || id >= MOTOR_COUNT) return;
  g_target[id] = clampSpeed(speed);
}

void motorStopAll() {
  for (int i = 0; i < MOTOR_COUNT; i++) g_target[i] = 0;
}

void motorStopAllNow() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    g_target[i] = 0;
    g_out[i] = 0;
    applyOutput(i, 0);
  }
}

void motorUpdate() {
  const uint32_t now = millis();
  const uint32_t dt = now - g_lastTickMs;
  if (dt < MOTOR_RAMP_TICK_MS) return;

  // 掉帧（比如串口堵塞）时不要一次跨太大，按 tick 数补偿但有上限
  uint32_t ticks = dt / MOTOR_RAMP_TICK_MS;
  if (ticks > 20) ticks = 20;
  g_lastTickMs = now;

  const int step = cfg().rampStep;
  const int maxDelta = step > 0 ? step * (int)ticks : 255;

  for (int i = 0; i < MOTOR_COUNT; i++) {
    const int t = g_target[i];
    const int o = g_out[i];
    if (o == t) continue;

    int next = o;
    if (maxDelta <= 0) {
      next = t;
    } else if (o < t) {
      next = (t - o <= maxDelta) ? t : o + maxDelta;
    } else {
      next = (o - t <= maxDelta) ? t : o - maxDelta;
    }

    g_out[i] = next;
    output(i, next);
  }
}

int motorTarget(int id) {
  if (id < 0 || id >= MOTOR_COUNT) return 0;
  return g_target[id];
}

int motorOutput(int id) {
  if (id < 0 || id >= MOTOR_COUNT) return 0;
  return g_out[id];
}
