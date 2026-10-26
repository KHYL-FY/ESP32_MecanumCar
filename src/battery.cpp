/*
 * battery.cpp —— 分压 + ADC 读电池电压
 *
 * 关键点：
 *  1) 用 analogReadMilliVolts()，它走 ESP32 内部的 eFuse 校准曲线，
 *     比 analogRead() 再去算准得多（非线性和衰减误差都处理掉了）
 *  2) 多次采样取平均 + 一阶低通，避免电机启停瞬间的电压跌落造成误判
 *  3) 阈值按 2S 锂电：8.4V 满，7.4V 标称，6.8V 提醒，6.4V 必须停
 */

#include "battery.h"
#include "config.h"

namespace {

// 2S 锂电阈值（整组电压）
const float kLowVolts = 6.80f;   // 3.40V/cell：该充电了
const float kCriticalVolts = 6.40f;  // 3.20V/cell：再放就损伤电池

const uint32_t kSampleIntervalMs = 250;
const int kSamplesPerRead = 8;

float g_volts = 0.0f;        // 滤波后的电压
bool g_valid = false;
uint32_t g_lastSampleMs = 0;

}  // namespace

void batteryBegin() {
  pinMode(BAT_ADC_PIN, INPUT);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);  // 量程 ~0~3.1V
  g_volts = 0.0f;
  g_valid = false;
  g_lastSampleMs = 0;
}

void batteryPoll() {
  if (!cfg().batEn) {
    g_valid = false;
    return;
  }

  const uint32_t now = millis();
  if (now - g_lastSampleMs < kSampleIntervalMs) return;
  g_lastSampleMs = now;

  // 引脚电压 = 电池电压 × 分压比；反推电池电压
  // batDivX10 = (R1+R2)/R2 × 10
  const float div = cfg().batDivX10 / 10.0f;
  float sumMv = 0;
  for (int i = 0; i < kSamplesPerRead; i++) {
    sumMv += analogReadMilliVolts(BAT_ADC_PIN);
  }
  const float pinMv = sumMv / kSamplesPerRead;
  float volts = pinMv * div / 1000.0f * (cfg().batTrim / 100.0f);

  // 没接分压时引脚会飘，低于 1V 认为无效
  if (volts < 1.0f) {
    g_valid = false;
    return;
  }

  // 一阶低通：起步直接赋值，之后慢慢跟，避免电机启动时的瞬时跌落误报
  if (!g_valid) {
    g_volts = volts;
    g_valid = true;
  } else {
    g_volts = g_volts * 0.75f + volts * 0.25f;
  }
}

float batteryVolts() {
  return g_valid ? g_volts : 0.0f;
}

float batteryCellVolts() {
  return batteryVolts() / 2.0f;
}

bool batteryValid() {
  return g_valid;
}

BatteryState batteryState() {
  if (!cfg().batEn || !g_valid) return BAT_OK;
  if (g_volts < kCriticalVolts) return BAT_CRITICAL;
  if (g_volts < kLowVolts) return BAT_LOW;
  return BAT_OK;
}

const char *batteryLabel() {
  if (!cfg().batEn) return "关闭";
  if (!g_valid) return "无读数";
  switch (batteryState()) {
    case BAT_CRITICAL: return "没电";
    case BAT_LOW:      return "低电";
    default:           return "正常";
  }
}
