/*
 * config.cpp —— 参数默认值 + NVS 读写
 */

#include "config.h"
#include <Preferences.h>

namespace {

Preferences g_prefs;
Settings g_settings;

const char *kNvsNamespace = "mecanum";

void applyDefaults() {
  for (int i = 0; i < WHEEL_COUNT; i++) g_settings.invert[i] = false;
  g_settings.minDuty = 45;    // 实测 TT 电机 8% 占空比左右才转得动
  g_settings.maxDuty = 255;   // 默认放开，试车时可手动调小
  g_settings.rampStep = 8;    // 255 / 8 * 5ms ≈ 160ms 到满速
  g_settings.expo = 0;        // 线性，最直观；嫌中位太灵敏再加
  g_settings.yawTrim = 0;     // 不修正，跑偏了再调
  g_settings.rotScale = 100;  // 原地转不降额
  g_settings.rotInv = true;   // 右摇杆右打 = 顺时针（符合直觉）
  g_settings.strafeInv = false;
  g_settings.batEn = false;   // 默认关闭：没焊分压电阻时别误报
  g_settings.batDivX10 = 31;  // 10k + 4.7k（或 100k + 47k）
  g_settings.batTrim = 100;   // 校准系数
}

// NVS 里的垃圾数据（比如手滑写进了 0）在这里兜住
void sanitize() {
  if (g_settings.minDuty > 200) g_settings.minDuty = 200;
  if (g_settings.maxDuty < 60) g_settings.maxDuty = 60;   // uint8_t 本身不会超过 255
  if (g_settings.rampStep > 50) g_settings.rampStep = 50;
  if (g_settings.expo > 80) g_settings.expo = 80;
  if (g_settings.yawTrim < -30) g_settings.yawTrim = -30;
  if (g_settings.yawTrim > 30) g_settings.yawTrim = 30;
  if (g_settings.rotScale < 40) g_settings.rotScale = 40;
  if (g_settings.rotScale > 100) g_settings.rotScale = 100;
  if (g_settings.batDivX10 < 15) g_settings.batDivX10 = 15;
  if (g_settings.batDivX10 > 60) g_settings.batDivX10 = 60;
  if (g_settings.batTrim < 85) g_settings.batTrim = 85;
  if (g_settings.batTrim > 115) g_settings.batTrim = 115;
  // 最小占空比不能超过输出上限，否则一动就是满速
  if (g_settings.minDuty > g_settings.maxDuty) g_settings.minDuty = g_settings.maxDuty;
}

}  // namespace

void configBegin() {
  applyDefaults();

  if (!g_prefs.begin(kNvsNamespace, false)) {
    Serial.println(F("[配置] NVS 打开失败，使用默认值（改动将不会保存）"));
    return;
  }

  g_settings.minDuty  = g_prefs.getUChar("minDuty",  g_settings.minDuty);
  g_settings.maxDuty  = g_prefs.getUChar("maxDuty",  g_settings.maxDuty);
  g_settings.rampStep = g_prefs.getUChar("rampStep", g_settings.rampStep);
  g_settings.expo     = g_prefs.getUChar("expo",     g_settings.expo);
  g_settings.rotScale = g_prefs.getUChar("rotScale", g_settings.rotScale);
  g_settings.yawTrim  = g_prefs.getChar("yawTrim",   g_settings.yawTrim);
  g_settings.rotInv   = g_prefs.getBool("rotInv",    g_settings.rotInv);
  g_settings.strafeInv = g_prefs.getBool("strafeInv", g_settings.strafeInv);
  g_settings.batEn     = g_prefs.getBool("batEn",     g_settings.batEn);
  g_settings.batDivX10 = g_prefs.getUChar("batDiv",   g_settings.batDivX10);
  g_settings.batTrim   = g_prefs.getChar("batTrim",   g_settings.batTrim);

  for (int i = 0; i < WHEEL_COUNT; i++) {
    char key[6];
    snprintf(key, sizeof(key), "inv%d", i);
    g_settings.invert[i] = g_prefs.getBool(key, g_settings.invert[i]);
  }

  g_prefs.end();
  sanitize();

  Serial.println(F("[配置] 已从 NVS 读取（改方向不用重新烧录了）"));
}

Settings &cfg() {
  return g_settings;
}

void configSave() {
  sanitize();
  if (!g_prefs.begin(kNvsNamespace, false)) return;

  g_prefs.putUChar("minDuty",  g_settings.minDuty);
  g_prefs.putUChar("maxDuty",  g_settings.maxDuty);
  g_prefs.putUChar("rampStep", g_settings.rampStep);
  g_prefs.putUChar("expo",     g_settings.expo);
  g_prefs.putUChar("rotScale", g_settings.rotScale);
  g_prefs.putChar("yawTrim",   g_settings.yawTrim);
  g_prefs.putBool("rotInv",    g_settings.rotInv);
  g_prefs.putBool("strafeInv", g_settings.strafeInv);
  g_prefs.putBool("batEn",     g_settings.batEn);
  g_prefs.putUChar("batDiv",   g_settings.batDivX10);
  g_prefs.putChar("batTrim",   g_settings.batTrim);

  for (int i = 0; i < WHEEL_COUNT; i++) {
    char key[6];
    snprintf(key, sizeof(key), "inv%d", i);
    g_prefs.putBool(key, g_settings.invert[i]);
  }

  g_prefs.end();
  Serial.println(F("[配置] 已保存到 NVS"));
}

void configReset() {
  applyDefaults();
  configSave();
  Serial.println(F("[配置] 已恢复默认"));
}

void configPrint() {
  static const char *names[WHEEL_COUNT] = {"左前", "右前", "左后", "右后"};
  Serial.println(F("---- 当前配置 ----"));
  for (int i = 0; i < WHEEL_COUNT; i++) {
    Serial.printf("  %s(%d) 方向 = %s\n", names[i], i + 1,
                  cfg().invert[i] ? "【反】" : "正");
  }
  Serial.printf("  最小占空比 = %u（电机转不动就调大）\n", cfg().minDuty);
  Serial.printf("  输出上限   = %u（k/K 调，越小越慢越好开）\n", cfg().maxDuty);
  Serial.printf("  加速斜坡   = %u / 5ms%s\n", cfg().rampStep,
                cfg().rampStep == 0 ? "（已关闭）" : "");
  Serial.printf("  摇杆曲线   = %u（x/X 调，越大中位越细腻）\n", cfg().expo);
  Serial.printf("  偏航修正   = %+d（z/Z 调，前进跑偏时用）\n", (int)cfg().yawTrim);
  Serial.printf("  转向力度   = %u%%（y/Y 调，原地转太快就降）\n", cfg().rotScale);
  Serial.printf("  转向极性   = %s（i 切换，右打=顺时针才对）\n",
                cfg().rotInv ? "正常" : "已反向");
  Serial.printf("  平移极性   = %s（u 切换，右打=向右才对）\n",
                cfg().strafeInv ? "已反向" : "正常");
  Serial.printf("  电压监测   = %s（b 开关，GPIO 34）\n",
                cfg().batEn ? "开" : "关");
  Serial.printf("  分压比     = %u.%u : 1（n/N 调）\n",
                cfg().batDivX10 / 10, cfg().batDivX10 % 10);
  Serial.printf("  电压校准   = %d%%（l/L 调，对齐万用表读数）\n", cfg().batTrim);
  Serial.println(F("------------------"));
}
