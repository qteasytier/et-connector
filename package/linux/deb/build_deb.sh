#!/bin/bash
set -e

# 项目根目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# 解析参数
VERSION=""
IS_PRO=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --pro)
            IS_PRO=true
            shift
            ;;
        *)
            echo "用法: $0 --version x.x.x [--pro]"
            exit 1
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "错误: --version 参数为必填项"
    echo "用法: $0 --version x.x.x [--pro]"
    exit 1
fi

# 路径定义
INSTALL_BIN="$PROJECT_DIR/Install/bin"
CONTROL_FILE="$SCRIPT_DIR/DEBIAN/control"
DESKTOP_DIR="$SCRIPT_DIR/usr"
FAVICON="$SCRIPT_DIR/opt/etconnector/favicon.png"
OUTPUT_DIR="$PROJECT_DIR/Install"

if $IS_PRO; then
    DEB_NAME="EasyTierProConnector_v${VERSION}_linux_amd64.deb"
    PKG_NAME="easytier-pro-connector"
    DESKTOP_NAME="ET Pro Connector"
    echo "=== EasyTier Pro Connector DEB 打包 ==="
else
    DEB_NAME="EasyTierConnector_v${VERSION}_linux_amd64.deb"
    PKG_NAME="easytier-connector"
    DESKTOP_NAME="ET Connector"
    echo "=== EasyTier Connector DEB 打包 ==="
fi

# 检查源文件是否存在
if [[ ! -d "$INSTALL_BIN" ]]; then
    echo "错误: Install/bin 目录不存在，请先完成构建"
    echo "  cd build && cmake .. && cmake --build . && cmake --install ."
    exit 1
fi

if [[ ! -f "$CONTROL_FILE" ]]; then
    echo "错误: control 文件不存在: $CONTROL_FILE"
    exit 1
fi

echo "版本号: $VERSION"
echo "输出文件: $DEB_NAME"

# 创建临时目录
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

# 构建 DEB 目录结构
mkdir -p "$TMP_DIR/DEBIAN"
mkdir -p "$TMP_DIR/opt/etconnector"

# 复制并替换 control 版本号 + 包名
cp "$CONTROL_FILE" "$TMP_DIR/DEBIAN/control"
sed -i "s/^Version:.*/Version: $VERSION/" "$TMP_DIR/DEBIAN/control"
sed -i "s/^Package:.*/Package: $PKG_NAME/" "$TMP_DIR/DEBIAN/control"
# 确保 control 文件以换行符结尾（避免 dpkg-deb 解析 Description 字段出错）
sed -i -e '$a\' "$TMP_DIR/DEBIAN/control"

# 复制 Install/bin 内容到 opt/etconnector
cp -r "$INSTALL_BIN"/* "$TMP_DIR/opt/etconnector/"

# 复制 favicon.png
if [[ -f "$FAVICON" ]]; then
    cp "$FAVICON" "$TMP_DIR/opt/etconnector/"
fi

# 复制桌面入口等文件，并替换 Name
if [[ -d "$DESKTOP_DIR" ]]; then
    cp -r "$DESKTOP_DIR" "$TMP_DIR/"
    # 替换桌面文件中的 Name
    find "$TMP_DIR/usr" -name "*.desktop" -exec sed -i "s/^Name=.*/Name=$DESKTOP_NAME/" {} \;
fi

# 设置目录权限
find "$TMP_DIR" -type d -exec chmod 755 {} \;
find "$TMP_DIR" -type f -exec chmod 644 {} \;
# 确保可执行文件有执行权限
find "$TMP_DIR/opt/etconnector" -type f -name "EasyTierConnector" -exec chmod 755 {} \;
find "$TMP_DIR/opt/etconnector" -type f -name "easytier-*" -exec chmod 755 {} \;

# 打包
echo "正在打包..."
dpkg-deb --build "$TMP_DIR" "$OUTPUT_DIR/$DEB_NAME"

echo "完成: $OUTPUT_DIR/$DEB_NAME"
