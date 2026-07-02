# AUR 发布指南

## 前置条件

1. **注册 AUR 账号**
   - 访问 https://aur.archlinux.org/ 注册账号

2. **安装基础工具**
   ```bash
   sudo pacman -S base-devel git
   ```

3. **配置 SSH 密钥**（用于推送 AUR）
   ```bash
   ssh-keygen -t ed25519 -C "your-email@example.com"
   cat ~/.ssh/id_ed25519.pub
   # 将公钥添加到 AUR 账户的 SSH Keys 页面
   ```

## 一键生成 AUR 文件

```bash
cd package/linux/aur
./build_aur.sh --version 1.0.0
```

执行后会生成：
- `PKGBUILD` — 构建脚本
- `.SRCINFO` — 源信息文件（makepkg 自动生成）

## 发布到 AUR

### 首次发布

```bash
# 克隆 AUR 仓库（空仓库）
git clone ssh://aur@aur.archlinux.org/easytier-connector.git

# 复制生成的文件
cp package/linux/aur/PKGBUILD easytier-connector/
cp package/linux/aur/.SRCINFO easytier-connector/

# 提交
cd easytier-connector
git add .
git commit -m "Initial commit for easytier-connector"
git push
```

### 后续更新

```bash
cd easytier-connector

# 重新生成 PKGBUILD 和 .SRCINFO（新版本）
../build_aur.sh --version 1.1.0

# 复制更新后的文件
cp PKGBUILD .SRCINFO easytier-connector/

# 提交更新
cd easytier-connector
git add .
git commit -m "Update to v1.1.0"
git push
```

## 本地验证

在发布前，建议在本地验证 PKGBUILD 是否正常工作：

```bash
cd easytier-connector
makepkg -si
```

这会编译并安装包，验证无误后再推送到 AUR。

## 常见问题

1. **makepkg 找不到 .SRCINFO？**
   - 确保在包含 PKGBUILD 的目录中运行 `makepkg --printsrcinfo > .SRCINFO`

2. **推送被拒绝？**
   - 检查 SSH 密钥是否正确添加到 AUR 账号
   - 首次推送可能需要等待 AUR 管理员审核

3. **源码下载失败？**
   - 确保 Gitee 仓库中存在对应 tag（如 `v1.0.0`）
   - 检查网络连接或尝试更换网络环境

## 参考文档

- [AUR 提交指南](https://wiki.archlinux.org/title/AUR_submission_guidelines)
- [PKGBUILD 格式](https://wiki.archlinux.org/title/PKGBUILD)
- [.SRCINFO 说明](https://wiki.archlinux.org/title/.SRCINFO)
