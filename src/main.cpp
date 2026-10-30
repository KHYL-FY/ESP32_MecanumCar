/*
 * ============================================================
 *  ESP32 麦轮小车 · 阶段 3：电机接线标定 + 遥控驱动
 * ============================================================
 *
 *  这一版的重点是【接线标定】，也就是把"代码里的左前轮"和
 *  "桌上真正会转的那个轮子"对上号，顺便把转向修正掉。
 *
 *  方向修正不用重新烧录：
 *      按 q / w / e / r → 翻转 左前 / 右前 / 左后 / 右后 的转向，
 *      立刻存进 NVS（掉电不丢），然后自动正转 1.5 秒给你看效果。
 *
 *  ------------------------------------------------------------
 *  串口命令（115200，直接敲单个字符，不用回车）
 *      1 2 3 4   左前 / 右前 / 左后 / 右后 正转 1.5 秒
 *      5 6 7 8   对应电机 反转 1.5 秒
 *      c         顺序自动标定：四个轮依次正转 1.2 秒
 *      q w e r   翻转 左前 / 右前 / 左后 / 右后 方向并保存
 *      0         立即急停并锁定（断电停车，不减速）
 *      a         串口解锁：不用按住 LB 也能推摇杆
 *      m         切换最小占空比 45 → 70 → 100 → 130
 *      v         查看当前配置
 *      d         恢复默认配置
 *      p         重新打印说明
 *
 *  遥控映射（默认必须按住 LB 才生效，松开立刻停）
 *      左摇杆 上下 → 前进 / 后退
 *      左摇杆 左右 → 左移 / 右移
 *      右摇杆 左右 → 原地左转 / 右转
 *
 *  接线要点
 *      TB6612 两片的 STBY 都接 3V3（不接电机不转）
 *      TB6612 的 VM 接 降压模块 5V；VCC 接 ESP32 3V3（逻辑电平匹配）
 *      所有 GND 必须共地
 * ============================================================
 */

#include <Arduino.h>
#include "remote.h"
#include "motor.h"
#include "config.h"
#include "battery.h"
#include "diag.h"

// ---------------- 可调参数 ----------------
static const int      REMOTE_RX_PIN   = 16;
static const int      STICK_DEADZONE  = 8;     // 摇杆死区（百分比）
static const uint32_t LOST_TIMEOUT_MS = 300;   // 手柄超时判定（毫秒）
static const uint32_t TEST_RUN_MS     = 1500;  // 单电机标定动作时长
static const int      TEST_SPEED      = 120;   // 标定转速

static const char *MOTOR_NAME[MOTOR_COUNT] = {"左前", "右前", "左后", "右后"};

// ---------------- 状态 ----------------
static bool     g_armedGate = false;  // 串口 'a' 解锁（调试用）
static bool     g_testActive = false; // 单电机标定进行中
static uint32_t g_testEndMs = 0;

// 顺序标定：-1 = 未运行；0~7 = 步号
static int      g_seqStep = -1;
static uint32_t g_seqEndMs = 0;

// ---------------- 工具 ----------------

static int clamp255(int v) {
  if (v > 255) return 255;
  if (v < -255) return -255;
  return v;
}

static int pctToSpeed(int pct) {
  return clamp255(pct * 255 / 100);
}

// 摇杆曲线：expo 越大，中位附近越细腻、末端越激进
// out = (1-k)*x + k*x^3   (x 归一化到 -1..1)
static int applyExpo(int pct) {
  const int e = cfg().expo;
  if (e <= 0 || pct == 0) return pct;
  const float x = pct / 100.0f;
  const float k = e / 100.0f;
  const float y = (1.0f - k) * x + k * x * x * x;
  return (int)lroundf(y * 100.0f);
}

static void startTest(int id, int speed) {
  g_seqStep = -1;  // 单电机测试优先，打断顺序标定
  g_testActive = true;
  g_testEndMs = millis() + TEST_RUN_MS;
  motorStopAll();
  motorSet(id, speed);
}

// ---------------- 顺序标定 ----------------

// -1 = 停顿，0~3 = 转对应电机
static const int8_t   kSeqMotor[8] = { -1, 0, -1, 1, -1, 2, -1, 3 };
static const uint16_t kSeqMs[8]    = { 500, 1200, 500, 1200, 500, 1200, 500, 1200 };

static void seqStart() {
  g_testActive = false;
  g_seqStep = 0;
  g_seqEndMs = millis() + kSeqMs[0];
  motorStopAll();
  Serial.println(F("\n>>> 顺序标定开始，注意看【哪个轮子在转】"));
}

// 进入第 step 步的动作
static void seqEnter(int step) {
  const int8_t m = kSeqMotor[step];
  if (m < 0) {
    motorStopAll();
    return;
  }
  motorSet(m, TEST_SPEED);
  Serial.printf("\n>>> 第 %d 个：%s轮 正转 1.2 秒（看它往哪边转）\n",
                m + 1, MOTOR_NAME[m]);
}

static void tickSeq(uint32_t now) {
  if (g_seqStep < 0) return;
  if (now < g_seqEndMs) return;

  g_seqStep++;
  if (g_seqStep >= 8) {
    g_seqStep = -1;
    motorStopAll();
    Serial.println(F("\n>>> 顺序标定结束"));
    Serial.println(F("    对不上的轮子，按 q(左前) w(右前) e(左后) r(右后) 校正"));
    return;
  }
  seqEnter(g_seqStep);
  g_seqEndMs = now + kSeqMs[g_seqStep];
}

// 启动时把第 0 步的动作做掉（停顿）
static void seqBegin() {
  seqStart();
  seqEnter(0);
}

// ---------------- 单电机标定 ----------------

static void tickTest(uint32_t now) {
  if (!g_testActive) return;
  if (now < g_testEndMs) return;

  motorStopAll();
  g_testActive = false;
  Serial.println(F("\n>>> 标定动作结束，电机已停"));
}

// ---------------- 配置命令 ----------------

static void flipMotor(int id) {
  cfg().invert[id] = !cfg().invert[id];
  configSave();
  Serial.printf("\n>>> %s 方向改为【%s】，正转 1.5 秒验证\n",
                MOTOR_NAME[id], cfg().invert[id] ? "反" : "正");
  startTest(id, TEST_SPEED);
}

static void cycleMinDuty() {
  uint8_t d = cfg().minDuty;
  if (d < 55)       d = 70;
  else if (d < 90)  d = 100;
  else if (d < 120) d = 130;
  else              d = 45;
  cfg().minDuty = d;
  configSave();
  Serial.printf("\n>>> 最小占空比 = %u\n", cfg().minDuty);
  if (d > 100) {
    Serial.println(F("    调这么高还是不动，就别调了——查接线（AIN/BIN、STBY、电源）"));
  }
}

// ---------------- 手感调参命令 ----------------

static void tunePrint(const __FlashStringHelper *name, long value,
                      const __FlashStringHelper *suffix = F("")) {
  Serial.print(F("\n>>> "));
  Serial.print(name);
  Serial.print(F(" = "));
  Serial.print(value);
  Serial.println(suffix);
}

static void tuneExpo(int dir) {
  int v = (int)cfg().expo + dir * 5;
  if (v < 0) v = 0;
  if (v > 80) v = 80;
  cfg().expo = (uint8_t)v;
  configSave();
  tunePrint(F("摇杆曲线 expo"), v, F("（越大，中位越细腻、末端越猛）"));
}

static void tuneYawTrim(int dir) {
  int v = (int)cfg().yawTrim + dir;
  if (v < -30) v = -30;
  if (v > 30) v = 30;
  cfg().yawTrim = (int8_t)v;
  configSave();
  Serial.printf("\n>>> 偏航修正 = %+d（正数=往右修，负数=往左修）\n", v);
}

static void tuneRotScale(int dir) {
  int v = (int)cfg().rotScale + dir * 5;
  if (v < 40) v = 40;
  if (v > 100) v = 100;
  cfg().rotScale = (uint8_t)v;
  configSave();
  Serial.printf("\n>>> 转向力度 = %d%%（越小原地转越温柔）\n", v);
}

static void tuneMaxDuty(int dir) {
  int v = (int)cfg().maxDuty + dir * 10;
  if (v < 60) v = 60;
  if (v > 255) v = 255;
  cfg().maxDuty = (uint8_t)v;
  configSave();
  Serial.printf("\n>>> 输出上限 = %d（越小越慢、越好开）\n", v);
}

static void printHelp() {
  Serial.println(F("------------------------------------------------------"));
  Serial.println(F("串口命令（敲单个字符，不用回车）"));
  Serial.println(F("  1 2 3 4   左前/右前/左后/右后 正转 1.5 秒"));
  Serial.println(F("  5 6 7 8   对应电机 反转 1.5 秒"));
  Serial.println(F("  c         顺序自动标定（四个轮依次转）"));
  Serial.println(F("  q w e r   翻转 左前/右前/左后/右后 方向并保存"));
  Serial.println(F("  0         立即急停并锁定"));
  Serial.println(F("  a         串口解锁（不用按住 LB 也能推摇杆）"));
  Serial.println(F("  m         切换最小占空比（电机转不动就按它）"));
  Serial.println(F("  --- 手感调参（改完立即可用，掉电保存）---"));
  Serial.println(F("  x / X     摇杆曲线  -5 / +5"));
  Serial.println(F("  z / Z     偏航修正  -1 / +1（前进跑偏用）"));
  Serial.println(F("  y / Y     转向力度  -5 / +5"));
  Serial.println(F("  k / K     输出上限 -10 / +10"));
  Serial.println(F("  i         转向极性翻转（拐弯方向反了按它）"));
  Serial.println(F("  u         平移极性翻转（左右横移反了按它）"));
  Serial.println(F("  --- 电池电压监测（GPIO34，需分压电阻）---"));
  Serial.println(F("  b         开关电压监测"));
  Serial.println(F("  n / N     分压比 -0.1 / +0.1（10k+4.7k → 3.1）"));
  Serial.println(F("  l / L     电压校准 -1% / +1%（对齐万用表）"));
  Serial.println(F("  --- 诊断 ---"));
  Serial.println(F("  g         打印事件日志（可反复看，不清空）"));
  Serial.println(F("  G         清空事件日志（开机次数不受影响）"));
  Serial.println(F("  ---"));
  Serial.println(F("  v         查看配置    d 恢复默认    p 打印说明"));
  Serial.println(F("------------------------------------------------------"));
}

static void handleSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();

    switch (c) {
      case '0':
        motorStopAllNow();
        g_testActive = false;
        g_seqStep = -1;
        g_armedGate = false;
        Serial.println(F("\n>>> 急停：已断电停车并锁定\n"));
        break;

      case 'a':
      case 'A':
        g_armedGate = !g_armedGate;
        Serial.printf("\n>>> 串口解锁 = %s（按住 LB 仍然优先）\n",
                      g_armedGate ? "开" : "关");
        break;

      case 'c':
      case 'C':
        seqBegin();
        break;

      case 'q': flipMotor(MOTOR_LF); break;
      case 'w': flipMotor(MOTOR_RF); break;
      case 'e': flipMotor(MOTOR_LR); break;
      case 'r': flipMotor(MOTOR_RR); break;

      case 'm':
      case 'M':
        cycleMinDuty();
        break;

      case 'x': tuneExpo(-1); break;
      case 'X': tuneExpo(+1); break;
      case 'z': tuneYawTrim(-1); break;
      case 'Z': tuneYawTrim(+1); break;
      case 'y': tuneRotScale(-1); break;
      case 'Y': tuneRotScale(+1); break;
      case 'k': tuneMaxDuty(-1); break;
      case 'K': tuneMaxDuty(+1); break;

      case 'i':
      case 'I':
        cfg().rotInv = !cfg().rotInv;
        configSave();
        Serial.printf("\n>>> 转向极性 = %s（右摇杆右打 → %s）\n",
                      cfg().rotInv ? "正常" : "已反向",
                      cfg().rotInv ? "顺时针" : "逆时针");
        break;

      case 'u':
      case 'U':
        cfg().strafeInv = !cfg().strafeInv;
        configSave();
        Serial.printf("\n>>> 平移极性 = %s（左摇杆右打 → 向%s平移）\n",
                      cfg().strafeInv ? "已反向" : "正常",
                      cfg().strafeInv ? "左" : "右");
        break;

      case 'b':
      case 'B':
        cfg().batEn = !cfg().batEn;
        configSave();
        Serial.printf("\n>>> 电压监测 = %s\n", cfg().batEn ? "开" : "关");
        if (cfg().batEn && !batteryValid()) {
          Serial.println(F("    读不到电压：检查 GPIO34 的分压电阻是否焊好，"));
          Serial.println(F("    或用 n / N 核对分压比（10k+4.7k → 3.1）"));
          Serial.println(F("    注意：不启用电量监测也不影响开车"));
        }
        break;

      case 'n':
        if (cfg().batDivX10 > 15) { cfg().batDivX10--; configSave(); }
        Serial.printf("\n>>> 分压比 = %u.%u : 1\n",
                      cfg().batDivX10 / 10, cfg().batDivX10 % 10);
        break;
      case 'N':
        if (cfg().batDivX10 < 60) { cfg().batDivX10++; configSave(); }
        Serial.printf("\n>>> 分压比 = %u.%u : 1\n",
                      cfg().batDivX10 / 10, cfg().batDivX10 % 10);
        break;

      case 'l':
        if (cfg().batTrim > 85) { cfg().batTrim--; configSave(); }
        Serial.printf("\n>>> 电压校准 = %d%%\n", cfg().batTrim);
        break;
      case 'L':
        if (cfg().batTrim < 115) { cfg().batTrim++; configSave(); }
        Serial.printf("\n>>> 电压校准 = %d%%\n", cfg().batTrim);
        break;

      case 'g':
        diagPrint();   // 只打印，不清空（可反复看）
        break;
      case 'G':
        diagClear();   // 大写 G 才清空
        break;

      case 'v':
      case 'V':
        configPrint();
        break;

      case 'd':
      case 'D':
        configReset();
        configPrint();
        break;

      case 'p':
      case 'P':
      case '?':
        printHelp();
        break;

      default:
        if (c >= '1' && c <= '8') {
          const int idx = (c - '1') % 4;
          const bool rev = (c >= '5');
          Serial.printf("\n>>> 标定：%s轮 %s转 1.5 秒\n",
                        MOTOR_NAME[idx], rev ? "反" : "正");
          startTest(idx, rev ? -TEST_SPEED : TEST_SPEED);
        }
        break;
    }
  }
}

// ---------------- 调试输出 ----------------

static void printStatus(uint32_t now, bool alive, bool busy) {
  static uint32_t lastPrint = 0;
  if (now - lastPrint < 250) return;
  lastPrint = now;

  const GamepadState &s = remoteState();

  const char *state = busy ? "标定" : (!alive ? "断开" : (g_armedGate || s.lb ? "驱动" : "锁定"));

  char bat[12];
  if (cfg().batEn && batteryValid()) {
    snprintf(bat, sizeof(bat), "%4.2fV", batteryVolts());
  } else {
    snprintf(bat, sizeof(bat), "  --  ");
  }

  Serial.printf("%-4s|%+4d%+4d%+4d%+4d|%+5d%+5d%+5d%+5d|%5lums|%s %s\n",
                state,
                remoteToPercent(s.lx), remoteToPercent(s.ly),
                remoteToPercent(s.rx), remoteToPercent(s.ry),
                motorOutput(MOTOR_LF), motorOutput(MOTOR_RF),
                motorOutput(MOTOR_LR), motorOutput(MOTOR_RR),
                (unsigned long)remoteAgeMs() > 99999UL ? 99999UL : (unsigned long)remoteAgeMs(),
                bat, batteryLabel());
}

// ---------------- 主流程 ----------------

// 电压报警：只在状态变化时提示一次，避免刷屏
static void batteryWarn(uint32_t now) {
  if (!cfg().batEn) return;

  static BatteryState lastState = BAT_OK;
  static uint32_t lastMs = 0;

  const BatteryState st = batteryState();
  if (st == lastState && now - lastMs < 5000) return;
  if (st == BAT_OK && lastState == BAT_OK) return;
  if (st == BAT_LOW && lastState == BAT_LOW && now - lastMs < 15000) return;

  lastState = st;
  lastMs = now;

  if (st == BAT_LOW) {
    Serial.printf("\n>>> 电池偏低 %.2fV（%.2fV/节）—— 该准备充电了\n",
                  batteryVolts(), batteryCellVolts());
  } else if (st == BAT_CRITICAL) {
    Serial.printf("\n>>> 电池 %.2fV 已经过低（%.2fV/节）！电机已强制断电\n",
                  batteryVolts(), batteryCellVolts());
    Serial.println(F("    2S 锂电放到 6.0V 以下会永久损坏，请立刻停车充电"));
  } else {
    Serial.printf("\n>>> 电池电压恢复正常 %.2fV\n", batteryVolts());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  diagBootBegin();   // 最先打印：开机次数 + 上次复位原因（判断是不是被电机拉垮复位的）

  configBegin();
  motorBegin();
  motorStopAllNow();

  batteryBegin();
  diagBegin();
  remoteBegin(REMOTE_RX_PIN);

  Serial.println();
  Serial.println(F("======================================================"));
  Serial.println(F("  ESP32 麦轮小车 · 阶段 3：电机标定 + 遥控驱动"));
  Serial.println(F("======================================================"));
  Serial.println(F("电机默认【锁定】。先做这三件事："));
  Serial.println(F("  1) 车架空，轮子离地"));
  Serial.println(F("  2) 按 c 顺序标定，看每个轮子转得对不对"));
  Serial.println(F("  3) 方向反了按 q w e r 校正（自动保存）"));
  Serial.println();
  configPrint();
  printHelp();
  Serial.println();
  Serial.println(F("状态 | 摇杆 LX LY RX RY | 四轮输出 LF RF LR RR | 数据龄 | 电池"));
}

void loop() {
  handleSerial();
  remotePoll();
  batteryPoll();

  const uint32_t now = millis();

  tickTest(now);
  tickSeq(now);
  batteryWarn(now);

  const bool busy = g_testActive || g_seqStep >= 0;
  const GamepadState &s = remoteState();
  const bool alive = remoteAlive(LOST_TIMEOUT_MS);
  const BatteryState bat = batteryState();

  // ---- 链路中断记录：把"断了多久"记下来，事后用 g 查看 ----
  static bool    wasAlive = true;
  static uint32_t lostAtMs = 0;
  if (!alive && wasAlive) {
    lostAtMs = now;
  } else if (alive && !wasAlive) {
    const uint32_t gap = now - lostAtMs;
    if (gap >= 400) diagLinkLost(gap);   // 400ms 以下的抖动不记，太多了
  }
  wasAlive = alive;

  // ---- 过放保护：只在刚触发的瞬间记一次，避免刷屏 ----
  static BatteryState lastBat = BAT_OK;
  if (bat == BAT_CRITICAL && lastBat != BAT_CRITICAL) {
    diagBatteryCritical(batteryVolts());
  }
  lastBat = bat;

  if (busy) {
    // 标定过程中遥控不参与，避免手一碰摇杆把车开跑
  } else if (bat == BAT_CRITICAL) {
    // 过放保护：电压已经到危险线，直接断电压车，避免损坏电池
    motorStopAllNow();
  } else if (!alive) {
    // 失控保护：手柄丢了立刻断电停车
    motorStopAllNow();
  } else {
    const bool armed = alive && (s.lb || g_armedGate);
    if (!armed) {
      motorStopAll();
    } else {
      const int lx = remoteToPercent(s.lx, STICK_DEADZONE);
      const int ly = remoteToPercent(s.ly, STICK_DEADZONE);
      const int rx = remoteToPercent(s.rx, STICK_DEADZONE);

      // 摇杆曲线（中位细腻、末端激进）
      const int vx = pctToSpeed(applyExpo(ly));   // 前后：左摇杆上下

      // 平移极性：默认"左摇杆右打 = 向右平移"；反了就按 u
      const int lxIn = cfg().strafeInv ? -lx : lx;
      const int vy = pctToSpeed(applyExpo(lxIn));

      // 转向极性：默认"右摇杆右打 = 顺时针"；反了就按 i
      const int rxIn = cfg().rotInv ? rx : -rx;
      int w = pctToSpeed(applyExpo(rxIn));
      w = w * cfg().rotScale / 100;               // 原地转向降额，更好控

      // 偏航修正：只在有前进/后退时生效，左右两侧差速补偿跑偏
      int vxL = vx, vxR = vx;
      const int trim = cfg().yawTrim;
      if (vx != 0 && trim != 0) {
        vxL = clamp255(vx * (100 + trim) / 100);
        vxR = clamp255(vx * (100 - trim) / 100);
      }

      // 麦轮逆运动学
      motorSet(MOTOR_LF, clamp255(vxL + vy + w));
      motorSet(MOTOR_RF, clamp255(vxR - vy - w));
      motorSet(MOTOR_LR, clamp255(vxL - vy + w));
      motorSet(MOTOR_RR, clamp255(vxR + vy - w));
    }
  }

  motorUpdate();  // 推进软启动斜坡（每圈都要调）

  // 记录"行驶中的最低电压"（判断电池是不是被瞬间拉垮）
  {
    const bool driving = !busy && alive && (s.lb || g_armedGate);
    diagSampleBattery(batteryVolts(), driving);
  }

  printStatus(now, alive, busy);
}
