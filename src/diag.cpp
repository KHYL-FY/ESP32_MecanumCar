/*
 * diag.cpp —— 事件环形缓冲 + 统计
 *
 * 只占几十字节 RAM，事件满了覆盖最旧的。
 */

#include "diag.h"
#include "battery.h"

#include <Preferences.h>
#include <esp_system.h>

namespace {

const uint8_t kMaxEvents = 8;

struct Event {
  uint32_t ms;        // 发生时刻（开机后毫秒）
  uint8_t  code;      // DiagCode
  uint16_t volts100;  // 当时的电池电压 ×100（0xFFFF = 无读数）
  uint32_t aux;       // 附加参数：断连时长(ms)
};

Event g_events[kMaxEvents];
uint8_t g_count = 0;
uint8_t g_head = 0;      // 下一个写入位置

// 统计
uint32_t g_linkLostCount = 0;
uint32_t g_longestLostMs = 0;
uint32_t g_batCritCount = 0;
uint16_t g_minDrivingVolts100 = 0xFFFF;  // 行驶中的最低电压

const char *codeName(uint8_t code) {
  switch (code) {
    case DIAG_LINK_LOST: return "手柄超时";
    case DIAG_BAT_CRIT:  return "电池过低";
    case DIAG_ESTOP:     return "急停";
    default:             return "未知";
  }
}

void push(uint8_t code, uint32_t aux) {
  const float v = batteryVolts();
  Event &e = g_events[g_head];
  e.ms = millis();
  e.code = code;
  e.volts100 = (v > 0.5f) ? (uint16_t)(v * 100.0f) : 0xFFFF;
  e.aux = aux;

  g_head = (g_head + 1) % kMaxEvents;
  if (g_count < kMaxEvents) g_count++;
}

}  // namespace

void diagBootBegin() {
  // 开机次数 + 欠压复位次数，都存 NVS（开机才写一次，不伤闪存）
  Preferences prefs;
  const bool ok = prefs.begin("diag", false);

  uint32_t boots = 1;
  uint32_t brownouts = 0;
  if (ok) {
    boots = prefs.getUInt("boots", 0) + 1;
    brownouts = prefs.getUInt("brownouts", 0);
    prefs.putUInt("boots", boots);
  }

  const esp_reset_reason_t r = esp_reset_reason();
  const char *name = "未知";
  switch (r) {
    case ESP_RST_POWERON:   name = "上电"; break;
    case ESP_RST_EXT:       name = "复位脚"; break;
    case ESP_RST_SW:        name = "软件复位"; break;
    case ESP_RST_PANIC:     name = "程序崩溃"; break;
    case ESP_RST_INT_WDT:   name = "中断看门狗"; break;
    case ESP_RST_TASK_WDT:  name = "任务看门狗"; break;
    case ESP_RST_WDT:       name = "看门狗"; break;
    case ESP_RST_DEEPSLEEP: name = "深睡唤醒"; break;
    case ESP_RST_BROWNOUT:  name = "【电压跌落】"; break;
    case ESP_RST_SDIO:      name = "SDIO"; break;
    default: break;
  }

  if (r == ESP_RST_BROWNOUT) {
    brownouts++;
    if (ok) prefs.putUInt("brownouts", brownouts);
  }
  if (ok) prefs.end();

  Serial.printf(F("[开机] 第 %lu 次；上次复位原因：%s\n"),
                (unsigned long)boots, name);
  if (brownouts > 0) {
    Serial.printf(F("[开机] 累计欠压复位 %lu 次\n"), (unsigned long)brownouts);
  }
  if (r == ESP_RST_BROWNOUT) {
    Serial.println(F("       ↑ 这是「电机启动把 5V 拉垮」的直接证据，"));
    Serial.println(F("         不是代码问题：查电池电压 / 5V 线径 / 加电容 / 降输出上限"));
  }
}

void diagBegin() {
  g_count = 0;
  g_head = 0;
  g_linkLostCount = 0;
  g_longestLostMs = 0;
  g_batCritCount = 0;
  g_minDrivingVolts100 = 0xFFFF;
}

void diagLinkLost(uint32_t durationMs) {
  g_linkLostCount++;
  if (durationMs > g_longestLostMs) g_longestLostMs = durationMs;
  push(DIAG_LINK_LOST, durationMs);
}

void diagBatteryCritical(float volts) {
  g_batCritCount++;
  push(DIAG_BAT_CRIT, (uint32_t)(volts * 100.0f));
}

void diagEstop() {
  push(DIAG_ESTOP, 0);
}

void diagSampleBattery(float volts, bool armed) {
  if (!armed || volts < 0.5f) return;
  const uint16_t v100 = (uint16_t)(volts * 100.0f);
  if (v100 < g_minDrivingVolts100) g_minDrivingVolts100 = v100;
}

void diagPrint() {
  Serial.println(F("\n---- 事件日志（最早的在上）----"));

  if (g_count == 0) {
    Serial.println(F("  （没有记录）"));
  } else {
    // 从最旧的开始打印
    uint8_t start = (g_count < kMaxEvents) ? 0 : g_head;
    for (uint8_t i = 0; i < g_count; i++) {
      const Event &e = g_events[(start + i) % kMaxEvents];
      const uint32_t s = e.ms / 1000;
      Serial.printf("  %3lu.%01lu s  %-8s",
                    (unsigned long)s, (unsigned long)((e.ms % 1000) / 100),
                    codeName(e.code));
      if (e.volts100 != 0xFFFF) {
        Serial.printf("  电压 %u.%02uV", e.volts100 / 100, e.volts100 % 100);
      } else {
        Serial.print(F("  电压 --"));
      }
      if (e.code == DIAG_LINK_LOST) {
        Serial.printf("  断了 %lums", (unsigned long)e.aux);
      }
      Serial.println();
    }
  }

  Serial.println(F("---- 统计 ----"));
  Serial.printf("  手柄超时次数   = %lu\n", (unsigned long)g_linkLostCount);
  Serial.printf("  最长一次断连   = %lums%s\n", (unsigned long)g_longestLostMs,
                g_longestLostMs >= 2000 ? "（这么长基本是链路/干扰，不是电压）" : "");
  Serial.printf("  电池过低次数   = %lu\n", (unsigned long)g_batCritCount);
  if (g_minDrivingVolts100 != 0xFFFF) {
    Serial.printf("  行驶中最低电压 = %u.%02uV%s\n", g_minDrivingVolts100 / 100,
                  g_minDrivingVolts100 % 100,
                  g_minDrivingVolts100 < 640 ? "（<6.4V，电池被拉垮了）" : "");
  } else {
    Serial.println(F("  行驶中最低电压 = --（没启用电压监测时测不到）"));
  }
  Serial.println(F("-----------------"));
  Serial.println(F("（打 g 会清空日志）"));

  diagBegin();
}
