#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 解析参数
VERSION=""
REL="1"
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --rel)
            REL="$2"
            shift 2
            ;;
        *)
            echo "用法: $0 --version x.x.x [--rel x]"
            exit 1
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "错误: --version 参数为必填项"
    echo "用法: $0 --version x.x.x [--rel x]"
    exit 1
fi


PKG_NAME="easytier-connector"
PKG_DESC="基于 Qt6 的系统托盘应用程序，用于连接 EasyTier Web 控制台（配置服务器）。EasyTier Web Connector based on Qt6."
echo "=== EasyTier Connector AUR 打包 ==="


echo "版本号: $VERSION"
echo "pkgrel: $REL"

# AUR 输出目录
AUR_DIR="$SCRIPT_DIR"

# 清理旧的生成文件
rm -f "$AUR_DIR/PKGBUILD" "$AUR_DIR/.SRCINFO" "$AUR_DIR/et-connector.install" "$AUR_DIR/easytier-pro-connector.install"

# 使用占位符 + sed 替换，避免 heredoc 中 $ 被 shell 提前展开
cat > "$AUR_DIR/PKGBUILD" <<'EOF'
# Maintainer: Myqfeng <viagrahuang@outlook.com>

pkgname=__PKGNAME__
pkgver=__VERSION__
pkgrel=__REL__
pkgdesc="__DESC__"
arch=('x86_64')
options=('!debug')
url="https://gitee.com/qteasytier/easytier-connector"
license=('LGPL3')
depends=('qt6-base' 'qt6-svg')
makedepends=('cmake' 'git')
install=__INSTALL_FILE__
source=("${pkgname}::git+https://gitee.com/qteasytier/easytier-connector.git#tag=${pkgver}")
sha256sums=('SKIP')

build() {
    cd "${srcdir}/${pkgname}"
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr
    cmake --build build -j"$(nproc)"
}

package() {
    cd "${srcdir}/${pkgname}"

    # 主程序
    install -Dm755 "build/Output/EasyTierConnector" \
        "${pkgdir}/opt/etconnector/EasyTierConnector"

    # 后端守护进程
    install -Dm755 "build/Output/qtet-connector-daemon" \
        "${pkgdir}/opt/etconnector/qtet-connector-daemon"

    # 动态库
    for lib in build/Output/*.so; do
        [ -f "$lib" ] && install -Dm644 "$lib" "${pkgdir}/opt/etconnector/"
    done

    # 图标
    install -Dm644 "favicon/favicon-open.png" \
        "${pkgdir}/opt/etconnector/favicon-open.png"

    # systemd 服务
    install -Dm644 "assets/easytier-connector.service" \
        "${pkgdir}/etc/systemd/system/easytier-connector.service"

    # 桌面文件
    install -Dm644 "package/linux/deb/usr/share/applications/etconnector.desktop" \
        "${pkgdir}/usr/share/applications/etconnector.desktop"

    # 创建 /usr/bin 软链接
    install -dm755 "${pkgdir}/usr/bin"
    ln -sf "/opt/etconnector/EasyTierConnector" \
        "${pkgdir}/usr/bin/EasyTierConnector"
}
EOF

# 替换占位符
sed -i "s/__PKGNAME__/$PKG_NAME/g" "$AUR_DIR/PKGBUILD"
sed -i "s/__VERSION__/$VERSION/g" "$AUR_DIR/PKGBUILD"
sed -i "s/__REL__/$REL/g" "$AUR_DIR/PKGBUILD"
sed -i "s/__DESC__/$PKG_DESC/g" "$AUR_DIR/PKGBUILD"
sed -i "s/__INSTALL_FILE__/${PKG_NAME}.install/g" "$AUR_DIR/PKGBUILD"

# 生成 .install 文件（systemd 服务启停）
cat > "$AUR_DIR/${PKG_NAME}.install" <<INSTALLEOF
post_install() {
    systemctl daemon-reload
    systemctl enable --now easytier-connector.service 2>/dev/null || true
}
post_upgrade() {
    systemctl daemon-reload
    systemctl restart easytier-connector.service 2>/dev/null || true
}
pre_remove() {
    systemctl stop easytier-connector.service 2>/dev/null || true
    systemctl disable easytier-connector.service 2>/dev/null || true
}
INSTALLEOF

# 生成 .SRCINFO
echo "正在生成 .SRCINFO..."
cd "$AUR_DIR"
makepkg --printsrcinfo > .SRCINFO

echo ""
echo "完成！"
echo "AUR 文件已生成到: $AUR_DIR"
echo "  - PKGBUILD"
echo "  - ${PKG_NAME}.install"
echo "  - .SRCINFO"
echo ""
echo "发布步骤:"
echo "  1. git clone ssh://aur@aur.archlinux.org/${PKG_NAME}.git"
echo "  2. 将 PKGBUILD、${PKG_NAME}.install 和 .SRCINFO 复制到 AUR 仓库目录"
echo "  3. git add . && git commit -m \"Update to v$VERSION\" && git push"
