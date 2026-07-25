// tools/common/json_escape.h
//
// TD-CLI-06: 公共 JSON 字符串转义实现，消除 tools/dspk-pack-cli 与
// tools/s2p-cli 中的 escapeJson 重复实现。
//
// 实现完整的 JSON 字符串转义（RFC 8259 第 7 节）：
//   - 反斜杠 (\) 与双引号 (") 转义
//   - 控制字符 \b \f \n \r \t 转义
//   - 其他控制字符 (< 0x20) 使用 \uXXXX 转义
//
// 不返回包含引号的字面量，调用方负责在两侧添加引号。
//
// 约束对齐：
// - CODING-05（模块设计 / DRY）：消除重复实现
// - ARCH-03（组合优于继承）：抽取共享实现而非复制粘贴
// - D-11（v3 不改动 v2 公共接口签名）：本文件为 tools 内部辅助，
//   不属于 lib/Core 公共 API，不违反 D-11

#pragma once

#include <string>

namespace synthrt::tools::common {

    /// Escape a string for inclusion in a JSON string literal (without the
    /// surrounding quotes). Mirrors lib/C/srt_v4.cpp's escapeJsonString
    /// behavior so all three JSON-emitting paths in tools/ stay consistent.
    std::string escapeJsonString(const std::string &s);

} // namespace synthrt::tools::common
