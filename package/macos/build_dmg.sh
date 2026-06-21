#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# 解析参数
IS_PRO=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --pro)
            IS_PRO=true
            shift
            ;;
        *)
            echo "用法: $0 [--pro]"
            exit 1
            ;;
    esac
done

APP_PATH="$PROJECT_DIR/Install/EasyTierConnector.app"
if [[ ! -d "$APP_PATH" ]]; then
    echo "错误: 找不到 $APP_PATH，请先完成构建安装"
    exit 1
fi

VERSION=$(grep 'project(EasyTierConnector VERSION' "$PROJECT_DIR/CMakeLists.txt" | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')
ARCH=$(uname -m)

if $IS_PRO; then
    VOLNAME="EasyTierProConnector"
    DMG_NAME="EasyTierProConnector_v${VERSION}_mac_${ARCH}.dmg"
    echo "=== EasyTier Pro Connector DMG 打包 ==="
else
    VOLNAME="EasyTierConnector"
    DMG_NAME="EasyTierConnector_v${VERSION}_mac_${ARCH}.dmg"
    echo "=== EasyTier Connector DMG 打包 ==="
fi

OUTPUT_DIR="$PROJECT_DIR/Install"

echo "版本: $VERSION"
echo "架构: $ARCH"

# --- 防御1: 卸载可能残留的同名已挂载卷 ---
if mount | grep -q "/Volumes/$VOLNAME"; then
    echo "检测到已挂载的同名卷 $VOLNAME，正在强制卸载..."
    hdiutil detach "/Volumes/$VOLNAME" -force 2>/dev/null || true
    sleep 1
fi

# --- 防御2: 删除残留输出文件（避免 -ov 在某些锁场景下失效）---
if [[ -f "$OUTPUT_DIR/$DMG_NAME" ]]; then
    echo "删除已存在的 DMG 文件..."
    rm -f "$OUTPUT_DIR/$DMG_NAME"
fi

# --- 防御3: 使用 workspace 内临时目录，避免 /var/folders/ 受 Spotlight/fsevents 干扰 ---
TMP_DIR="$PROJECT_DIR/build/_dmg_tmp"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

cp -R "$APP_PATH" "$TMP_DIR/"
ln -s /Applications "$TMP_DIR/Applications"

# 等待文件系统操作完成，减少 fsevents/Spotlight 干扰窗口
sync
sleep 1

echo "正在创建 DMG..."

# --- 防御4: 重试机制（最多3次，间隔递增）---
RETRY=0
MAX_RETRY=3
while [ $RETRY -lt $MAX_RETRY ]; do
    if hdiutil create -volname "$VOLNAME" \
        -srcfolder "$TMP_DIR" \
        -ov -format UDZO \
        "$OUTPUT_DIR/$DMG_NAME" 2>&1; then
        echo "完成: $OUTPUT_DIR/$DMG_NAME"
        exit 0
    fi
    RETRY=$((RETRY + 1))
    if [ $RETRY -lt $MAX_RETRY ]; then
        WAIT=$((RETRY * 2))
        echo "hdiutil 第 ${RETRY} 次尝试失败，${WAIT}秒后重试..."
        sleep $WAIT
    fi
done

echo "错误: hdiutil 创建 DMG 失败（已重试 ${MAX_RETRY} 次）"
exit 1
