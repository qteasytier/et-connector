#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

PKG_NAME="easytier-connector"

echo "=== EasyTier Connector DEB 打包 ==="

# 解析参数
OUTPUT_DIR=""
VERSION=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --output-dir)
            OUTPUT_DIR="$(cd "$2" && pwd)"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        *)
            echo "用法: $0 --output-dir <Output路径> --version x.x.x"
            exit 1
            ;;
    esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "错误: --output-dir 参数为必填项"
    echo "用法: $0 --output-dir <Output路径> --version x.x.x"
    exit 1
fi

if [[ -z "$VERSION" ]]; then
    echo "错误: --version 参数为必填项"
    echo "用法: $0 --output-dir <Output路径> --version x.x.x"
    exit 1
fi

if [[ ! -d "$OUTPUT_DIR" ]]; then
    echo "错误: Output 目录不存在: $OUTPUT_DIR"
    exit 1
fi

echo "Output 目录: $OUTPUT_DIR"
echo "版本号: $VERSION"

# 验证 Output 目录包含必要产物
EXE_PATH="$OUTPUT_DIR/EasyTierConnector"
DAEMON_PATH="$OUTPUT_DIR/qtet-connector-daemon"

if [[ ! -f "$EXE_PATH" ]]; then
    echo "错误: Output 目录中未找到 EasyTierConnector"
    exit 1
fi

if [[ ! -f "$DAEMON_PATH" ]]; then
    echo "错误: Output 目录中未找到 qtet-connector-daemon"
    exit 1
fi

# 项目资源文件
CONTROL_FILE="$SCRIPT_DIR/DEBIAN/control"
FAVICON="$PROJECT_DIR/favicon/favicon-open.png"
SERVICE_FILE="$PROJECT_DIR/assets/easytier-connector.service"
DESKTOP_FILE="$PROJECT_DIR/assets/etconnector.desktop"

for f in "$CONTROL_FILE" "$FAVICON" "$SERVICE_FILE" "$DESKTOP_FILE"; do
    if [[ ! -f "$f" ]]; then
        echo "错误: 资源文件不存在: $f"
        exit 1
    fi
done

DEB_NAME="EasyTierConnector_v${VERSION}_linux_amd64.deb"
PKG_OUTPUT_DIR="$OUTPUT_DIR/package"

echo "输出包: $PKG_OUTPUT_DIR/$DEB_NAME"

# 创建临时目录构建 deb
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

mkdir -p "$TMP_DIR/DEBIAN"
mkdir -p "$TMP_DIR/opt/etconnector"
mkdir -p "$TMP_DIR/usr/bin"
mkdir -p "$TMP_DIR/etc/systemd/system"

# 复制主程序和守护进程
cp "$EXE_PATH" "$TMP_DIR/opt/etconnector/"
cp "$DAEMON_PATH" "$TMP_DIR/opt/etconnector/"

# 复制动态库
for lib in "$OUTPUT_DIR"/*.so; do
    [ -f "$lib" ] && cp "$lib" "$TMP_DIR/opt/etconnector/"
done

# 复制图标
cp "$FAVICON" "$TMP_DIR/opt/etconnector/"

# 复制 systemd 服务
cp "$SERVICE_FILE" "$TMP_DIR/etc/systemd/system/"

# 复制桌面文件
mkdir -p "$TMP_DIR/usr/share/applications"
cp "$DESKTOP_FILE" "$TMP_DIR/usr/share/applications/"

# 创建 /usr/bin 软链接
ln -sf "/opt/etconnector/EasyTierConnector" "$TMP_DIR/usr/bin/EasyTierConnector"

# 复制并生成 control 文件
sed "s/^Version:.*/Version: $VERSION/" "$CONTROL_FILE" > "$TMP_DIR/DEBIAN/control"
sed -i -e '$a\' "$TMP_DIR/DEBIAN/control"

# 生成 postinst (安装后脚本)
cat > "$TMP_DIR/DEBIAN/postinst" <<'POSTINSTEOF'
#!/bin/bash
set -e

systemctl daemon-reload 2>/dev/null || true
systemctl enable easytier-connector.service 2>/dev/null || true
systemctl start easytier-connector.service 2>/dev/null || true
POSTINSTEOF
chmod 755 "$TMP_DIR/DEBIAN/postinst"

# 生成 postrm (卸载脚本)
cat > "$TMP_DIR/DEBIAN/postrm" <<'POSTRMEOF'
#!/bin/bash
set -e

if [[ "$1" == "remove" || "$1" == "purge" ]]; then
    systemctl stop easytier-connector.service 2>/dev/null || true
    systemctl disable easytier-connector.service 2>/dev/null || true
    systemctl daemon-reload 2>/dev/null || true
fi
POSTRMEOF
chmod 755 "$TMP_DIR/DEBIAN/postrm"

# 设置权限
find "$TMP_DIR" -type d -exec chmod 755 {} \;
find "$TMP_DIR" -type f ! -path "$TMP_DIR/DEBIAN/*" -exec chmod 644 {} \;
find "$TMP_DIR/opt/etconnector" -type f \( -name "EasyTierConnector" -o -name "easytier-*" -o -name "qtet-*" \) -exec chmod 755 {} \;

# 打包
mkdir -p "$PKG_OUTPUT_DIR"
echo "正在打包..."
dpkg-deb --build "$TMP_DIR" "$PKG_OUTPUT_DIR/$DEB_NAME"

echo ""
echo "完成！deb 包已生成: $PKG_OUTPUT_DIR/$DEB_NAME"
