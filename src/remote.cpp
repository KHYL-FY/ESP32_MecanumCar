/*
 * remote.cpp —— 塔克 X-SBTCOM1 手柄数据解析实现
 * 详见 remote.h 里的协议注释
 */

#include "remote.h"

namespace {

const uint8_t  HDR0 = 0xAA;
const uint8_t  HDR1 = 0x55;
const uint8_t  FRAME_CODE_GAMEPAD = 0xA0;  // 帧号：蓝牙手柄
const uint8_t  TYPE_STATUS  = 0x00;        // 类型：连接/断开提示
const uint8_t  TYPE_STICK   = 0x01;        // 类型：标准手柄数据
const uint8_t  STATUS_CONNECTED    = 0x55;
const uint8_t  STATUS_DISCONNECTED = 0xFF;

const size_t   BUF_MAX   = 32;
const size_t   MIN_FRAME = 7;   // 连接提示帧 7 字节

HardwareSerial *g_port = nullptr;
GamepadState    g_state;

uint8_t  g_buf[BUF_MAX];
size_t   g_len = 0;
size_t   g_want = 0;
uint8_t  g_sum = 0;

uint32_t g_badFrames = 0;

// 解析一帧完整数据（已在调用处通过校验）
void handleFrame(const uint8_t *f, size_t n) {
  // 帧号必须是手柄
  if (f[3] != FRAME_CODE_GAMEPAD) return;

  const uint8_t type = f[4];

  if (type == TYPE_STATUS) {
    // 连接提示帧：AA 55 07 A0 00 55/FF CK
    if (n >= 7) {
      if (f[5] == STATUS_CONNECTED) {
        g_state.connected = true;
      } else if (f[5] == STATUS_DISCONNECTED) {
        g_state.connected = false;
      }
    }
    return;
  }

  if (type != TYPE_STICK) return;
  if (n < 15) return;

  g_state.lx = (int8_t)f[5];
  g_state.ly = (int8_t)f[6];
  g_state.rx = (int8_t)f[7];
  g_state.ry = (int8_t)f[8];

  g_state.dpad = (f[9] <= 8) ? (uint8_t)f[9] : (uint8_t)DPAD_NONE;

  const uint8_t b2 = f[10];  // 按键2
  g_state.a  = (b2 >> 0) & 1;
  g_state.b  = (b2 >> 1) & 1;
  g_state.x  = (b2 >> 3) & 1;
  g_state.y  = (b2 >> 4) & 1;
  g_state.lb = (b2 >> 6) & 1;
  g_state.rb = (b2 >> 7) & 1;

  const uint8_t b3 = f[11];  // 按键3
  g_state.lt     = (b3 >> 0) & 1;
  g_state.rt     = (b3 >> 1) & 1;
  g_state.select = (b3 >> 3) & 1;
  g_state.start  = (b3 >> 4) & 1;
  g_state.lstick = (b3 >> 5) & 1;
  g_state.rstick = (b3 >> 6) & 1;

  g_state.rtrigger = f[12];
  g_state.ltrigger = f[13];

  g_state.lastFrameMs = millis();
  g_state.framesTotal++;

  // 收到标准数据帧，说明链路肯定是通的
  g_state.connected = true;
}

}  // namespace

void remoteBegin(int rxPin, uint32_t baud) {
  // 只开 RX，不占用 TX 引脚
  static HardwareSerial s(2);
  g_port = &s;
  g_port->begin(baud, SERIAL_8N1, rxPin, -1);

  g_len = 0;
  g_want = 0;
  g_sum = 0;
  g_state = GamepadState();
}

void remotePoll() {
  if (!g_port) return;

  while (g_port->available() > 0) {
    const uint8_t b = (uint8_t)g_port->read();

    // ---- 找帧头 AA 55 ----
    if (g_len == 0) {
      if (b == HDR0) { g_buf[g_len++] = b; }
      continue;
    }
    if (g_len == 1) {
      if (b == HDR1) {
        g_buf[g_len++] = b;
      } else {
        g_len = 0;
        if (b == HDR0) { g_buf[g_len++] = b; }
      }
      continue;
    }

    // ---- 第 3 字节是整帧长度 ----
    if (g_len == 2) {
      if (b < MIN_FRAME || b > BUF_MAX) { g_len = 0; continue; }
      g_buf[g_len++] = b;
      g_want = b;
      g_sum = (uint8_t)(HDR0 + HDR1 + b);
      continue;
    }

    // ---- 收剩下的字节 ----
    g_buf[g_len++] = b;

    if (g_len == g_want) {
      const uint8_t chk = g_buf[g_want - 1];
      if ((uint8_t)(g_sum & 0xFF) == chk) {
        handleFrame(g_buf, g_want);
      } else {
        g_badFrames++;
      }
      g_len = 0;
    } else {
      g_sum = (uint8_t)(g_sum + b);
    }
  }
}

const GamepadState &remoteState() { return g_state; }

uint32_t remoteAgeMs() {
  if (g_state.lastFrameMs == 0) return 0xFFFFFFFFUL;
  return millis() - g_state.lastFrameMs;
}

bool remoteAlive(uint32_t timeoutMs) {
  return g_state.framesTotal > 0 && remoteAgeMs() <= timeoutMs;
}

int remoteToPercent(int8_t raw, int deadzone) {
  const int a = (int)raw;
  if (deadzone < 0) deadzone = 0;
  if (deadzone > 126) deadzone = 126;
  if (a > deadzone)  return (a - deadzone) * 100 / (127 - deadzone);
  if (a < -deadzone) return (a + deadzone) * 100 / (127 - deadzone);
  return 0;
}
