#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复 VS Code 报 "无法打开 源 文件 freertos/FreeRTOS.h" 之类的问题。

原因：PlatformIO 新版把 ESP-IDF 的头文件路径写在一个响应文件
      (.../framework-arduinoespressif32-esp32-libs/flags/includes) 里，
      靠编译器的 "-iprefix + @响应文件" 机制展开。
      但 VS Code 的 C/C++ 插件不解析 @响应文件，于是 freertos/、esp_system/
      这些框架头文件路径全部丢失，编辑器的红波浪线就出来了。

      （注意：这只影响编辑器的语法检查，不影响 pio run 编译——
        真正编译时编译器是能读响应文件的。）

做法：把这个响应文件里的路径全部展开成绝对路径，写进
      .vscode/c_cpp_properties.json 的 includePath。

用法：
    python tools/fix_intellisense.py

什么时候需要重跑：
    PlatformIO 每次 Build 都会重新生成 c_cpp_properties.json，
    如果又出现红波浪线，再跑一次这个脚本即可。
"""

import json
import os
import re
import sys

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_PACKAGES = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
LIBS_PKG = os.path.join(PIO_PACKAGES, "framework-arduinoespressif32-esp32-libs")
INCLUDE_ROOT = os.path.join(LIBS_PKG, "include")
FLAGS_FILE = os.path.join(LIBS_PKG, "flags", "includes")
TARGET = os.path.join(PROJECT_DIR, ".vscode", "c_cpp_properties.json")


def main():
    if not os.path.isfile(FLAGS_FILE):
        print(f"[!] 找不到 PlatformIO 的响应文件：\n    {FLAGS_FILE}")
        print("    → 先执行一次 pio run，让它把框架包装好。")
        return 1

    if not os.path.isfile(TARGET):
        print(f"[!] 找不到 IntelliSense 配置：\n    {TARGET}")
        print("    → 先在 PlatformIO 里执行一次 Build，让它生成该文件。")
        return 1

    # 1) 从响应文件里取出所有 -iwithprefixbefore 的相对路径
    with open(FLAGS_FILE, "r", encoding="utf-8", errors="ignore") as f:
        flags_text = f.read()
    rel_paths = re.findall(r"-iwithprefixbefore\s+([^\s]+)", flags_text)

    expanded = []
    for rel in rel_paths:
        abs_path = os.path.normpath(os.path.join(INCLUDE_ROOT, rel.replace("/", os.sep)))
        if os.path.isdir(abs_path):
            expanded.append(abs_path.replace("\\", "/"))

    # 2) 读 c_cpp_properties.json（是 JSONC，先去掉 // 行注释）
    with open(TARGET, "r", encoding="utf-8") as f:
        raw = f.read()
    raw = "\n".join(
        line for line in raw.splitlines() if not line.strip().startswith("//")
    )
    cfg = json.loads(raw)

    if not cfg.get("configurations"):
        print("[!] 配置里没有 configurations 段，放弃。")
        return 1

    conf = cfg["configurations"][0]

    # 3) 合并 includePath（保留已有的，去重，末尾的空字符串要留在最后）
    old = conf.get("includePath", [])
    seen = {x.lower() for x in old if x}
    added = 0

    insert_at = len(old)
    if old and old[-1] == "":
        insert_at = len(old) - 1

    new_paths = []
    for p in expanded:
        if p.lower() not in seen:
            new_paths.append(p)
            seen.add(p.lower())
            added += 1

    conf["includePath"] = old[:insert_at] + new_paths + old[insert_at:]

    # 4) browse.path 同步（代码跳转用）
    browse = conf.setdefault("browse", {})
    browse.setdefault("limitSymbolsToIncludedHeaders", True)
    old_browse = browse.get("path", [])
    browse["path"] = [p for p in conf["includePath"] if p]

    # 5) 写回
    with open(TARGET, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=4, ensure_ascii=False)
        f.write("\n")

    print("[OK] 已修复 IntelliSense 头文件路径")
    print(f"     扫描到框架路径 : {len(expanded)} 条")
    print(f"     本次新增       : {added} 条")
    print(f"     写入文件       : {TARGET}")
    print()
    print("下一步：在 VS Code 里按 Ctrl+Shift+P → 执行")
    print("      C/C++: Reset IntelliSense Database")
    print("然后重新打开 main.cpp 查看，红波浪线应消失。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
