# DS Bank 模块 (`ds::bank`)

namespace: `ds::bank` | target: `srt-ds::bank` | 头文件: `include/diffsinger/Bank/`

---

## 职责

DS Bank 模块负责声库包的扫描、解析和查询：
- `VoicebankScanner` — 扫描声库目录，构建 SingerSnapshot 列表
- `PackageParser` — 解析声库包 manifest
- `PackageValidator` — 验证声库包完整性
- `JsonSchemaValidator` — JSON Schema 验证

---

## 关键 API

### VoicebankScanner

```cpp
// include/diffsinger/Bank/VoicebankScanner.h
namespace ds::bank;

class VoicebankScanner {
public:
    VoicebankScanner();
    ~VoicebankScanner();

    // 设置搜索路径（多次调用追加，clear() 重置）
    void setSearchPaths(const std::vector<std::filesystem::path> &paths);
    void clear();

    // 扫描所有搜索路径，解析每个包的 desc.json
    // 仅查找声库包，不扫描 G2P 包
    Expected<std::vector<PackageStatus>> refresh();

    // 缓存的扫描结果
    const std::vector<SingerSnapshot> &singers() const;

    // 按 ref 查找快照
    Expected<SingerSnapshot> singerSnapshot(const SingerRef &ref) const;

    // 按 singerId 查找（扫描所有包）
    Expected<SingerRef> findSinger(const std::string &singerId) const;

    // 按 singerId + packageId + version 精确查找（v3 BF-07 新增）
    // 空 packageId/version 表示不过滤（向后兼容）
    Expected<SingerRef> findSinger(const std::string &singerId,
                                   const std::string &packageId,
                                   const std::string &version) const;

    // 获取 packageId 对应的包目录（已弃用，仅返回首个匹配）
    [[deprecated("Use packageDirectories(packageId). Will be removed in Level=3.")]]
    std::filesystem::path packageDirectory(const std::string &packageId) const;

    // V3-01 §1.6：返回 (version, path) 列表，多版本同 packageId 全部保留
    std::vector<PackageDirectoryResult> packageDirectories(
        const std::string &packageId) const;
};
```

### PackageDirectoryResult (V3-01)

```cpp
// include/diffsinger/Bank/VoicebankScanner.h
namespace ds::bank;

struct PackageDirectoryResult {
    stdc::VersionNumber version;
    std::filesystem::path path;
};
```

`packageDirectories(packageId)` 返回所有匹配条目，按发现顺序排列；`packageId` 未知时返回空 vector。这是 `packageDirectory(packageId)` 的版本感知替代——后者只返回首个匹配，多版本同 packageId 会合并到一条，调用方需迁移到 `packageDirectories` 以获得完整版本隔离（V3-01 第 5 层）。`packageDirectory` 已标记 `[[deprecated]]`，将在 Level=3 移除。

### SingerRef

```cpp
// include/diffsinger/Bank/SingerRef.h
struct SingerRef {
    std::string packageId;
    std::string singerId;
    std::string version;  // v2 新增，归一化版本字符串（VersionNumber::toString()）
};
```

### SingerSnapshot

```cpp
// include/diffsinger/Bank/SingerSnapshot.h
struct SingerSnapshot {
    SingerRef ref;
    srt::core::DisplayText name;   // 多语言名称（全部翻译随对象携带），不再是拍扁后的 std::string
    ResolutionState resolutionState = ResolutionState::Pending;
    srt::core::Diagnostic resolutionError;
    double phonemeLength = 48.0;  // 固定 48，不可修改
    std::vector<std::string> languages;
    std::vector<std::string> speakerIds;
    std::string defaultLanguage;
    std::vector<std::string> inferenceIds;
    std::string version;  // 归一化版本字符串，镜像 ref.version
};
```

### PackageStatus

```cpp
// include/diffsinger/Bank/PackageStatus.h
struct PackageStatus {
    std::string packageId;
    stdc::VersionNumber version;
    std::filesystem::path rootPath;
    std::vector<std::string> dependencies;
    std::vector<std::string> unresolvedDependencies;
    bool valid = false;
    srt::core::Diagnostic error;
};
```

### ResolutionState

```cpp
// include/diffsinger/Bank/ResolutionState.h
enum class ResolutionState {
    Resolved,
    Pending,
    Missing,
};
```

---

## 调用关系

```
宿主层
  ├── scanner.setSearchPaths(voicebankPaths)
  ├── scanner.refresh()
  │     └── 遍历搜索路径，解析 desc.json → SingerSnapshot 列表
  │
  ├── scanner.findSinger(singerId, packageId, version)
  │     └── 返回精确匹配的 SingerRef
  │
  ├── scanner.packageDirectory(packageId)
  │     └── 返回包目录路径（用于 Runtime::loadPackage 和 LanguageService）
  │
  └── scanner.singerSnapshot(ref)
        └── 返回完整 SingerSnapshot
```

---

## 与其他模块的协作

- **LanguageService**: `packageDirectory()` 的返回值用于构建 `LanguageService::initialize()` 的 `packageDirs` 参数
- **ds-infer**: `SingerRef` 传递给 `SingerStageResolver::resolve()` 进行 stage 解析
- **Runtime**: `packageDirectory()` 的返回值传递给 `Runtime::loadPackage()`

**关键约束** (PACK-02): `VoicebankScanner` 只扫描 `desc.json`，不扫描 G2P 包。G2P 包由 `LanguageService` 处理。二者通过 `packageId→directory` 值表协作。

---

## 多语言字段（DisplayText 透传）

自本次本地化改造起，ds-bank 的所有人读字段（`LanguageInfo::name()`、`SpeakerInfo::name()`、
`SingerManifest::name()`、`PackageManifest::name()/description()/author()/license()`、
`SingerSnapshot::name`）返回 **`srt::core::DisplayText`**，保存 desc.json / singer config
中的**全部翻译**（键原样保留，含 `_` 默认值）。

变更要点：

- **解析期不再拍扁**：旧 `PackageParser::nameField()`（按 `m_displayLocale` 当场解析出唯一
  字符串并丢弃翻译表）已删除，改为 `displayTextField()` 调用
  `DisplayText::fromJsonValueTolerant` 整表保留。这是宿主（ds-editor-lite）切换 UI 语言
  无需重扫声库的前提——宿主对缓存的 DisplayText 按自有匹配策略随取随用。
- **Runtime 不做任何匹配（透传模型）**：`text(key)` 按键原样精确直取、区分大小写，缺键返回
  `nullptr`；不做 Lookup 截断、不做大小写折叠、不自动回退 `_`。宿主的典型取词：
  遍历 `locales()` 或按自有候选键链逐个 `text(key)`，全部落空时取 `text()`（`_`）。
  细节见 core.md §DisplayText/DisplayPath。
- **`setDisplayLocale()` 整条链移除**：`PackageParser::setDisplayLocale` 与
  `VoicebankScanner::setDisplayLocale/displayLocale` 已删除，无 no-op 兼容层。
- **字段名以 spec 2.4 为准**：desc.json 的许可字段只认 `copyright`（解析进
  `PackageManifest::license()`）；历史写法 `license` 不再读取，PackageValidator 按
  extra key 出 Warning 提示迁移。
- **Validator 合规检查**：多语言对象缺 `_` 默认项报 **Error**（spec 仍规定 `_` 必选）；
  非 `_` 键含 `_`（POSIX 写法如 `zh_CN`）报 **Warning** 并建议改用 BCP 47 写法——键对
  Runtime 不透明，POSIX 键是合法数据，仅为兼容性引导。扫描端仍宽松载入
  （`fromJsonValueTolerant`）。
- `name()` 返回值用于拼接字符串/比较时，用 `.text()`（默认）或按自有策略 `.text(key)`；空判定用
  `.isEmpty()`；构造/赋值接受 `std::string`（作为默认文本隐式转换）；`DisplayText` 没有
  字符串比较重载，旧断言 `name == "..."` 需改为 `name().text() == "..."`。

## 版本匹配

v3 修复了多个版本匹配 Bug：
- BF-02: `findSnapshot` 现在匹配 version 字段
- BF-07: `findSinger` 三参重载支持 packageId + version 过滤
- BF-21: 版本比较使用 `VersionNumber` 语义匹配（"1.0" == "1.0.0" == "1.0.0.0"）

`ref.version` 为空时不做版本过滤（兼容旧调用方）；非空时使用 `VersionNumber` 语义比较（`"1.0"` 与 `"1.0.0"` 视为相等），因为 `VersionNumber::toString()` 会归一化掉尾部零。

---

## 错误处理

Package 错误使用 `ErrorCode::Package*` 代码段（100-199）和 `Error::packageError()` 工厂函数：`PackageRootInvalid`、`PackageManifestInvalid`、`PackageManifestMissingField`、`PackageDependencyMissing`、`PackageDependencyCycle`、`PackageVersionConflict`、`PackageSingerConfigInvalid`、`PackageDuplicate` 等。

BF-33: `PackageParser::parsePackage` 现在遵守 `ParseMode` 参数。Strict 模式下，singer/inference 配置文件读取失败或 JSON 解析失败时返回 `PackageManifestInvalid` 错误（fail-fast），不再静默跳过。Relaxed 模式保持容错行为（跳过损坏项继续解析）。修复前 `parsePackage` 通过 `(void) mode;` 丢弃了 mode 参数，Strict 和 Relaxed 行为完全相同。
