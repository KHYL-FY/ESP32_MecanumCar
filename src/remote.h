/*
 * remote.h —— 塔克 X-SBTCOM1 蓝牙串口接收器 + G6S 手柄 解析模块
 *
 * 协议依据：《多功能蓝牙串口遥控模块用户手册 V1.0.0》6.2 节
 *
 * 帧格式（15 字节标准手柄数据）：
 *   [0] AA           帧头1
 *   [1] 55           帧头2
 *   [2] 0x0F         帧长（整帧字节数）
 *   [3] 0xA0         帧号（0xA0 = 蓝牙手柄）
 *   [4] 0x01         类型（0x01 = 标准手柄数据）
 *   [5] 左摇杆 X     ±127
 *   [6] 左摇杆 Y     ±127
 *   [7] 右摇杆 X     ±127
 *   [8] 右摇杆 Y     ±127
 *   [9] 按键1        方向键：0无 1上 2右上 3右 4右下 5下 6左下 7左 8左上
 *  [10] 按键2        bit7=RB bit6=LB bit4=Y bit3=X bit1=B bit0=A
 *  [11] 按键3        bit6=右摇杆按下 bit5=左摇杆按下 bit4=START
 *                    bit3=SELECT bit1=RT bit0=LT
 *  [12] 右扳机       0~255
 *  [13] 左扳机       0~255
 *  [14] 校验         前面 14 字节累加和低 8 位
 *
 * 连接提示帧（7 字节，类型 0x00）：
 *   [0] AA [1] 55 [2] 0x07 [3] 0xA0 [4] 0x00
 *   [5] 0x55 = 已连接 / 0xFF = 已断开
 *   [6] 校验
 */

#pragma once

#include <Arduino.h>

// 方向键取值
enum DpadValue : uint8_t {
  DPAD_NONE = 0,
  DPAD_UP, DPAD_UP_RIGHT, DPAD_RIGHT, DPAD_DOWN_RIGHT,
  DPAD_DOWN, DPAD_DOWN_LEFT, DPAD_LEFT, DPAD_UP_LEFT,
};

struct GamepadState {
  // 摇杆：-127..127，0 = 中位
  int8_t lx = 0, ly = 0, rx = 0, ry = 0;

  // 方向键：0..8
  uint8_t dpad = DPAD_NONE;

  // 按键（true = 按下）
  bool a = false, b = false, x = false, y = false;
  bool lb = false, rb = false;          // 肩键
  bool lt = false, rt = false;          // 扳机开关位
  bool start = false, select = false;
  bool lstick = false, rstick = false;  // 摇杆按下

  // 扳机模拟量：0..255
  uint8_t rtrigger = 0, ltrigger = 0;

  // 链路状态
  bool     connected = false;   // 手柄是否已连接（依据连接提示帧 + 超时）
  uint32_t lastFrameMs = 0;     // 最后一帧有效数据的时间
  uint32_t framesTotal = 0;     // 累计有效帧
};

void remoteBegin(int rxPin, uint32_t baud = 115200);

// 每次 loop 调用，把串口里攒的字节都消化掉
void remotePoll();

// 取当前状态（只读）
const GamepadState &remoteState();

// 距离上一帧有效数据过去了多少毫秒（从未收到过则返回一个很大的值）
uint32_t remoteAgeMs();

// 是否还在有效期内（超时判据，用于失控保护）
bool remoteAlive(uint32_t timeoutMs);

// 把摇杆原始值转成带死区的百分比 -100..100
int remoteToPercent(int8_t raw, int deadzone = 6);
