# ffmpeg-builds

预编译 FFmpeg 动态库的 vcpkg 端口，不做任何源码编译。

| 平台 | 来源 | 说明 |
| --- | --- | --- |
| Windows x64 / arm64 | [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) | mingw-w64 交叉编译，但**自带 MSVC 用的 `.lib` 导入库**，DLL 只依赖 UCRT 与系统库 |
| Linux x64 / arm64 | 同上 | glibc ≥ 2.28 |
| macOS arm64 / x86_64 | [PyAV-Org/pyav-ffmpeg](https://github.com/PyAV-Org/pyav-ffmpeg) | BtbN 不出 macOS；该仓库随 FFmpeg 版本按月发布，带头文件和 dylib |

## 特性

| 特性 | 作用 |
| --- | --- |
| `avcodec` `avformat` `avfilter` `avdevice` `swscale` `swresample` | 选择安装哪些库（`avutil` 恒装）。默认全装 |
| `gpl` | 换成 GPL 变体（多 x264/x265 等，整个程序会被 GPL 传染）。macOS 上游只有 GPL 构建，该开关被忽略 |
| `master` | 用 FFmpeg master 快照替代 8.1 分支（macOS 不支持） |
| `ffmpeg7` | 用 FFmpeg 7.1 分支替代 8.1（macOS 不支持） |
| `tools` | 额外装 `ffmpeg`/`ffprobe`/`ffplay` 到 `tools/ffmpeg-builds`（macOS 不支持，上游不带可执行文件） |

## 用法

```cmake
find_package(ffmpeg-builds CONFIG REQUIRED)
target_link_libraries(app PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::swresample)
```

目标名与 vcpkg 官方 `ffmpeg` 端口的 `FindFFMPEG.cmake` 一致，同时也导出
`FFMPEG_INCLUDE_DIRS` / `FFMPEG_LIBRARY_DIRS` / `FFMPEG_LIBRARIES` 变量。

**不要和官方 `ffmpeg` 端口同时安装**，头文件与运行库会撞车。

## 升级版本

上游资产名里带 git describe 串，SHA512 也只能自己算，因此清单 `assets.cmake` 由脚本生成：

```powershell
$env:GITHUB_TOKEN = 'ghp_...'   # 可选，避开 API 匿名限额
pwsh -File update-assets.ps1 -BtbnTag autobuild-2026-06-30-13-34 -PyavTag 8.1.2-1
```

脚本只用跨平台的 cmdlet，macOS / Linux 上装了 PowerShell 7（`brew install powershell` /
`apt install powershell`）即可照样运行；Windows 上用自带的 `powershell.exe -File` 也行。

选 tag 时注意两点：

* **BtbN 必须挑月末那次 `autobuild-*`**。其保留策略是「每月最后一次构建保留两年，日构建只留最近 14 个」，
  挑普通日构建的话两周后链接就 404 了；`latest` tag 内容每天变，SHA512 会失效，更不能用。
* 一个 BtbN release 里同时挂着多条分支（`-shared-8.1` / `-shared-7.1` / master），
  脚本用 `-ReleaseBranches` 指定收录哪几条，`-PyavBranch` 指定 macOS 对应哪条。

改完清单记得同步 `vcpkg.json` 的 `version` 与 `README` / 特性说明。

## 已知限制

* 只搬运上游产物，改不了 FFmpeg 的 configure 选项。要定制只能自建构建仓库。
* 没有 Debug 二进制，`debug/` 下放的是同一份 Release 库。
* 只支持 dynamic linkage 的三元组。
* macOS 版的私有依赖（x264/x265/opus/webp…）装在 `lib/ffmpeg-builds/` 下，
  由 `@loader_path` 引用，部署时要连同该子目录一起拷；
  其 configure 选项比 BtbN 少（无 libsoxr / libopenmpt / libvorbis 编码器等），但原生解码器齐全。
* **macOS arm64 的上游 dylib 是 `minos 14.0`**（x86_64 那份是 `minos 11.0`）。
  若三元组的 `VCPKG_OSX_DEPLOYMENT_TARGET` 低于 14.0，链接时会出现
  `built for newer macOS version (14.0) than being linked` 警告，产物也跑不了 macOS 13。
  arm64-osx 三元组建议显式设 `set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)`。
* Windows x64（MSVC 链接 + 运行）、Linux x64（GCC 链接 + 运行）、macOS arm64（clang 链接 + 运行）
  均已实测；macOS x64 只验到安装与 `install_name` 改写（手上没 Intel 机跑）。
  winarm64 / linuxarm64 未验证。
