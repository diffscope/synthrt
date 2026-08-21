<#
.SYNOPSIS
    重新生成 ffmpeg-builds 端口的下载清单 assets.cmake。

.DESCRIPTION
    ffmpeg-builds 直接消费第三方预编译产物，vcpkg 要求每个下载项都带 SHA512，
    而上游只发布 SHA256（BtbN）或什么都不发布（pyav-ffmpeg），所以哈希只能自己下载后算。
    本脚本负责：枚举 release 资产 -> 下载 -> 算 SHA512 -> 写出 assets.cmake。

    Windows / Linux 产物来自 BtbN/FFmpeg-Builds。注意其保留策略：
    「每月最后一次构建保留两年，其余日构建只留最近 14 个」，
    所以 -BtbnTag 必须挑月末那次 autobuild，否则两周后链接就 404 了。

    macOS 产物来自 PyAV-Org/pyav-ffmpeg（BtbN 不出 macOS）。

    BtbN 的资产名里带 git describe 串，只能通过 GitHub API 查。匿名调用每小时 60 次，
    很容易撞限额：设置 $env:GITHUB_TOKEN，或用 -BtbnReleaseJson 传一份缓存下来的
    `/repos/BtbN/FFmpeg-Builds/releases/tags/<tag>` 响应。

.EXAMPLE
    $env:GITHUB_TOKEN = 'ghp_xxx'
    pwsh -File update-assets.ps1 -BtbnTag autobuild-2026-06-30-13-34 -PyavTag 8.1.2-1
#>
[CmdletBinding()]
param(
    # BtbN/FFmpeg-Builds 的 release tag，务必选月末那次 autobuild
    [Parameter(Mandatory = $true)][string] $BtbnTag,

    # PyAV-Org/pyav-ffmpeg 的 release tag（macOS 用）
    [Parameter(Mandatory = $true)][string] $PyavTag,

    # 要收录的 BtbN release 分支。一个 release 里同时挂着多条分支（资产名以 -shared-<分支> 结尾）
    [string[]] $ReleaseBranches = @('8.1', '7.1'),

    # macOS 只有 pyav 一个来源，它对应哪条分支
    [string] $PyavBranch = '8.1',

    # 输出文件，默认写到本脚本所在目录的 assets.cmake
    [string] $OutFile,

    # 下载缓存目录，重复运行可复用
    [string] $CacheDir = (Join-Path ([IO.Path]::GetTempPath()) 'ffmpeg-builds-assets'),

    # 可选：预先抓好的 BtbN release JSON，避开 GitHub API 限额
    [string] $BtbnReleaseJson,

    # 可选：GitHub token，默认取 $env:GITHUB_TOKEN
    [string] $Token = $env:GITHUB_TOKEN
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not $OutFile) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $OutFile = Join-Path $scriptDir 'assets.cmake'
}
# 末尾用 [IO.File] 写文件，它按进程工作目录解析相对路径（与 PowerShell 的当前位置未必一致），先转绝对
if (-not [IO.Path]::IsPathRooted($OutFile)) {
    $OutFile = Join-Path (Get-Location).ProviderPath $OutFile
}

$BTBN_REPO = 'BtbN/FFmpeg-Builds'
$PYAV_REPO = 'PyAV-Org/pyav-ffmpeg'

# BtbN 的资产命名里带 git describe 串（每次构建都变），所以按正则匹配而不是拼字符串。
#   release 线: ffmpeg-n8.1.2-21-gce3c09c101-win64-lgpl-shared-8.1.zip
#   master  线: ffmpeg-N-125365-g9a01c1cb6a-win64-lgpl-shared.zip
$BTBN_TARGETS = @('win64', 'winarm64', 'linux64', 'linuxarm64')
$BTBN_VARIANTS = @('gpl', 'lgpl')

# pyav-ffmpeg 的资产名是固定的，不需要查 API
$PYAV_TARGETS = @('macos-arm64', 'macos-x86_64')

function Get-Release([string] $repo, [string] $tag) {
    $headers = @{ 'User-Agent' = 'ffmpeg-builds-update-assets' }
    if ($Token) { $headers['Authorization'] = "Bearer $Token" }
    Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/tags/$tag" `
        -Headers $headers -TimeoutSec 120 -UseBasicParsing
}

function Get-Sha512([string] $url, [string] $localName) {
    $path = Join-Path $CacheDir $localName
    if (Test-Path $path) {
        Write-Host "  命中缓存 $localName"
    }
    else {
        Write-Host "  下载 $localName"
        Invoke-WebRequest -Uri $url -OutFile $path -TimeoutSec 1800 -UseBasicParsing
    }
    (Get-FileHash -Path $path -Algorithm SHA512).Hash.ToLowerInvariant()
}

New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

function ConvertTo-LineKey([string] $branch) {
    # 8.1 -> v8_1，用作 CMake 变量名的一段
    'v' + ($branch -replace '\.', '_')
}

$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# 由 update-assets.ps1 生成，请勿手工编辑。')
$lines.Add('#')
$lines.Add('# 每条记录: FFMPEG_BUILDS_<line>_<target>_<variant>_{FILE,LOCAL,SHA512}')
$lines.Add('#   line   = v8_1 | v7_1 | master（release 分支写成 v<主>_<次>）')
$lines.Add('#   target = win64 | winarm64 | linux64 | linuxarm64 | macos-arm64 | macos-x86_64')
$lines.Add('')

# ---------------------------------------------------------------- BtbN (win/linux)
if ($BtbnReleaseJson) {
    Write-Host "读取缓存的 release JSON: $BtbnReleaseJson"
    $btbn = Get-Content -Path $BtbnReleaseJson -Raw | ConvertFrom-Json
}
else {
    Write-Host "读取 $BTBN_REPO @ $BtbnTag"
    $btbn = Get-Release $BTBN_REPO $BtbnTag
}
$lines.Add("set(FFMPEG_BUILDS_BTBN_TAG `"$BtbnTag`")")
$lineKeys = @($ReleaseBranches | ForEach-Object { ConvertTo-LineKey $_ }) + 'master'
$lines.Add("set(FFMPEG_BUILDS_LINES `"$($lineKeys -join ';')`")")
$lines.Add('')

foreach ($branch in ($ReleaseBranches + @('master'))) {
    $isMaster = ($branch -eq 'master')
    $line = if ($isMaster) { 'master' } else { ConvertTo-LineKey $branch }
    $branchRe = [regex]::Escape($branch)

    if (-not $isMaster) {
        # 该分支的完整版本号（如 8.1.2）从资产名里解析
        $sample = $btbn.assets |
            Where-Object { $_.name -match "^ffmpeg-n\d+\.\d+.*-win64-lgpl-shared-$branchRe\.zip$" } |
            Select-Object -First 1
        if (-not $sample) { throw "在 $BtbnTag 里找不到 $branch 分支的 win64 lgpl shared 资产" }
        $null = $sample.name -match '^ffmpeg-n(?<ver>\d+\.\d+(\.\d+)?)'
        $lines.Add("set(FFMPEG_BUILDS_${line}_VERSION `"$($Matches['ver'])`")")
    }

    foreach ($target in $BTBN_TARGETS) {
        foreach ($variant in $BTBN_VARIANTS) {
            $ext = if ($target -like 'win*') { 'zip' } else { 'tar\.xz' }
            $pattern = if ($isMaster) {
                "^ffmpeg-N-\d+-.*-$target-$variant-shared\.$ext$"
            }
            else {
                "^ffmpeg-n[\d.]+-.*-$target-$variant-shared-$branchRe\.$ext$"
            }
            $asset = $btbn.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
            if (-not $asset) {
                Write-Warning "跳过 $line/$target/$variant：$BtbnTag 里没有匹配 $pattern 的资产"
                continue
            }
            Write-Host "[$line/$target/$variant] $($asset.name)"
            $url = "https://github.com/$BTBN_REPO/releases/download/$BtbnTag/$($asset.name)"
            $sha = Get-Sha512 $url $asset.name
            $key = "FFMPEG_BUILDS_${line}_${target}_${variant}"
            $lines.Add("set(${key}_FILE `"$($asset.name)`")")
            $lines.Add("set(${key}_SHA512 `"$sha`")")
        }
    }
    $lines.Add('')
}

# ---------------------------------------------------------------- pyav-ffmpeg (macOS)
Write-Host "$PYAV_REPO @ $PyavTag"
$pyavLine = ConvertTo-LineKey $PyavBranch
$lines.Add("set(FFMPEG_BUILDS_PYAV_TAG `"$PyavTag`")")
$lines.Add("set(FFMPEG_BUILDS_PYAV_LINE `"$pyavLine`")")
foreach ($target in $PYAV_TARGETS) {
    $name = "ffmpeg-$target.tar.gz"
    # 资产名不含版本号，落地时改名，避免不同 tag 的包在 vcpkg downloads 目录里撞名
    $local = "pyav-ffmpeg-$PyavTag-$target.tar.gz"
    Write-Host "[$pyavLine/$target/gpl] $name"
    $url = "https://github.com/$PYAV_REPO/releases/download/$PyavTag/$name"
    $sha = Get-Sha512 $url $local
    $key = "FFMPEG_BUILDS_${pyavLine}_${target}_gpl"
    $lines.Add("set(${key}_FILE `"$name`")")
    $lines.Add("set(${key}_LOCAL `"$local`")")
    $lines.Add("set(${key}_SHA512 `"$sha`")")
}
$lines.Add('')

# 不用 Set-Content -Encoding utf8：PS 5.1 会写 BOM、pwsh 7 不写，两边生成的文件会差 3 字节。
# 这里显式指定「无 BOM UTF-8 + LF」，保证任何平台任何版本跑出来的清单逐字节一致。
[IO.File]::WriteAllText($OutFile, ($lines -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "已写出 $OutFile"
