#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Task/G2pTask.h>

#include "LangIdMap.h"

namespace srt::g2p::plugins::Multig2p {

    /// bundle.json 元数据（v7-stable 1.0 schema）。
    ///
    /// 字段详见 docs/stable/01-onnx-bundle.md §4：
    ///   bundle_version / schema_version / model_version / min_runtime_version /
    ///   vocab_hash / opset_version / export_flags / files / languages / generated_at
    struct BundleMeta {
        std::string bundleVersion;        // 期望 "1.0"
        std::string schemaVersion;        // "1.0"
        std::string modelVersion;         // "v1" 或 model_name
        std::string minRuntimeVersion;    // "onnxruntime>=1.13"
        std::string vocabHash;            // SHA-256 前 16 hex（与 vocabulary.json 交叉校验）
        int opsetVersion = 16;            // 固定 16
        std::vector<std::string> exportFlags; // v7-stable 默认 []
        std::unordered_map<std::string, std::string> files; // 逻辑名 → 文件名
        std::vector<std::string> languages; // lang_ref 列表
        std::string generatedAt;          // ISO 8601 UTC
    };

    /// Vocabulary 数据：symbols / global_symbols / vocab_hash。
    struct VocabularyData {
        std::vector<std::string> symbols;       // 全部符号（含 {lang}/{variant}/{symbol} 前缀）
        std::vector<std::string> globalSymbols; // 全局符号（如 <unk>/<pad>/<bos>/<eos>）
        std::string vocabHash;                  // SHA-256 前 16 hex

        // 便捷索引（约定顺序：<unk> <pad> <bos> <eos>）
        int unkIdx = 0;
        int padIdx = 1;
        int bosIdx = 2;
        int eosIdx = 3;

        /// symbol → id 查询；缺失返回 -1。
        int lookup(const std::string &symbol) const;

        /// id → 符号字符串（含前缀）；越界返回空字符串。
        const std::string &symbolAt(int id) const;

        /// id → 音素字符串（剥首个 {lang}/{variant}/ 前缀）。
        std::string phonemeAt(int id) const;

        /// 计算词表哈希（与 MultiG2p Vocabulary.hash 一致：SHA-256 前 16 hex，
        /// 符号间用 \x1f 分隔）。用于与 bundleMeta.vocabHash 交叉校验。
        std::string computeHash() const;
    };

    /// BundleLoader：加载 bundle.json + vocabulary.json + config.json。
    ///
    /// 加载流程（docs/stable/02-synthrt-plugin.md §4.1）：
    ///   1. loadBundleJson()：解析 bundle.json，提取版本信息与 languages 列表
    ///   2. loadVocabulary()：解析 vocabulary.json，提取 symbols/global_symbols/vocab_hash
    ///   3. 校验 bundleMeta.vocabHash == vocabulary.vocabHash（交叉校验）
    ///   4. buildLangIdMap()：从 bundleMeta.languages 构造 LangIdMap
    ///   5. （可选）loadConfig()：解析 config.json，读取 inference.default_max_len 等
    class BundleLoader {
    public:
        /// 加载 bundle.json。失败返回 ErrorCode::G2pConfigError。
        static srt::core::Expected<BundleMeta>
        loadBundleJson(const std::filesystem::path &bundlePath);

        /// 加载 vocabulary.json。失败返回 ErrorCode::G2pConfigError。
        static srt::core::Expected<VocabularyData>
        loadVocabulary(const std::filesystem::path &vocabPath);

        /// 从 bundle 目录解析 ONNX 文件路径（按 files 字段映射）。
        /// 缺失字段返回空 optional，调用方决定是否报错。
        static std::optional<std::filesystem::path>
        resolveOnnxFile(const BundleMeta &meta,
                        const std::filesystem::path &bundleDir,
                        const std::string &logicalName);

    private:
        /// 通用 JSON 文件读取：返回 JsonValue，失败返回 ErrorCode::G2pFileSystemError。
        static srt::core::Expected<srt::core::JsonValue>
        readJsonFile(const std::filesystem::path &path);
    };

}
