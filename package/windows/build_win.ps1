$ErrorActionPreference = "Stop"

# 解析参数
$Version = ""
$IsPro = $false
$i = 0
while ($i -lt $args.Count) {
    switch ($args[$i]) {
        "--version" {
            $i++
            if ($i -lt $args.Count) { $Version = $args[$i] }
        }
        "--pro" {
            $IsPro = $true
        }
        default {
            Write-Host "用法: $($MyInvocation.MyCommand.Name) --version x.x.x [--pro]"
            exit 1
        }
    }
    $i++
}

if (-not $Version) {
    Write-Host "错误: --version 参数为必填项"
    Write-Host "用法: $($MyInvocation.MyCommand.Name) --version x.x.x [--pro]"
    exit 1
}

# 脚本所在目录 → 项目根目录
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path "$ScriptDir\..\.."

# 根据版本选择 ISS 文件和应用名称
if ($IsPro) {
    $IssFile = Join-Path $ScriptDir "innosetup-pro.iss"
    $AppTitle = "EasyTier Pro Connector"
    $OutputBase = "EasyTierProConnector_v${Version}_win_amd64.exe"
} else {
    $IssFile = Join-Path $ScriptDir "innosetup.iss"
    $AppTitle = "EasyTier Connector"
    $OutputBase = "EasyTierConnector_v${Version}_win_amd64.exe"
}

$InstallBin = Join-Path $ProjectDir "Install\bin"
$InstallerOutput = Join-Path $ProjectDir "Install"

Write-Host "=== $AppTitle Inno Setup ====" -ForegroundColor Cyan
Write-Host "项目目录 : $ProjectDir"
Write-Host "版本号   : $Version"
Write-Host "输出目录 : $InstallerOutput"
Write-Host ""

# 检查 Install/bin 目录是否存在
if (-not (Test-Path $InstallBin)) {
    Write-Host "错误: Install/bin 目录不存在，请先完成构建" -ForegroundColor Red
    Write-Host "  cd build; cmake ..; cmake --build .; cmake --install ."
    exit 1
}

# 检查 iss 文件是否存在
if (-not (Test-Path $IssFile)) {
    Write-Host "错误: ISS 文件不存在: $IssFile" -ForegroundColor Red
    exit 1
}

# 查找 Inno Setup 编译器 (ISCC.exe)
function Find-ISCC {
    # 1. 从注册表查找
    $regPaths = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
        "HKLM:\SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"
    )
    foreach ($regPath in $regPaths) {
        try {
            $installLocation = (Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue).InstallLocation
            if ($installLocation) {
                $iscc = Join-Path $installLocation "ISCC.exe"
                if (Test-Path $iscc) { return $iscc }
            }
        } catch {}
    }

    # 2. 常见安装路径
    $commonPaths = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    )
    foreach ($path in $commonPaths) {
        if (Test-Path $path) { return $path }
    }

    # 3. PATH 环境变量
    $fromPath = (Get-Command "ISCC.exe" -ErrorAction SilentlyContinue).Source
    if ($fromPath) { return $fromPath }

    return $null
}

$ISCC = Find-ISCC
if (-not $ISCC) {
    Write-Host "错误: 未找到 Inno Setup 编译器 (ISCC.exe)" -ForegroundColor Red
    Write-Host "请安装 Inno Setup 6: https://jrsoftware.org/isinfo.php"
    exit 1
}
Write-Host "ISCC.exe : $ISCC"

# 调用 ISCC 编译
Write-Host "正在打包..."
$prevErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"

& $ISCC "/DMyAppVersion=$Version" "/DProjectDir=$ProjectDir" $IssFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "错误: ISCC 编译失败 (退出码: $LASTEXITCODE)" -ForegroundColor Red
    $ErrorActionPreference = $prevErrorAction
    exit $LASTEXITCODE
}
$ErrorActionPreference = $prevErrorAction

Write-Host "完成: $InstallerOutput\$OutputBase" -ForegroundColor Green
