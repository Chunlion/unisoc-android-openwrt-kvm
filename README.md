# Android ARM64 OpenWrt 虚拟机

在已 Root 且支持 KVM/TUN 的 Android ARM64 设备上，用 `crosvm` 运行 OpenWrt。默认只让 USB 或 Wi-Fi 热点连接的客户端经过 OpenWrt；Android 自己的应用和 SIM 流量保持原来的网络路径。

## 安装前确认

- Windows 已安装 WSL；在 WSL 中能执行 `adb` 或 `adb.exe`。
- 手机已通过 ADB 连接，且 KernelSU 或 Magisk 已授予 Root。
- 手机具备 `/dev/kvm` 和 `/dev/net/tun`。
- 首次安装需要 WSL 能联网下载 OpenWrt 镜像；缺少工具时脚本会通过 `sudo apt-get` 自动安装。
- 推荐使用 USB 有线 ADB。安装后热点/Wi-Fi 会接入 OpenWrt 网桥，Wi-Fi ADB 可能断开。
- 手机中不能已有 `/data/local/openwrt`。脚本不会覆盖已有虚拟机；请先备份，或确认不再需要后执行卸载。

在 WSL 中检查连接和设备能力：

```bash
adb devices
adb shell "su 0 sh -c 'test -c /dev/kvm && test -c /dev/net/tun'"
```

两条命令都成功后再安装。多台设备同时连接时，在 `config.env` 中设置 `ADB_SERIAL`。

## 首次安装

进入项目目录后执行一条命令：

```bash
cd /mnt/c/Users/<你的 Windows 用户名>/Desktop/Github/unisoc-android-openwrt-kvm
bash deploy-openwrt.sh install
```

首次安装会下载镜像、准备虚拟磁盘、上传到手机并启动 OpenWrt。默认虚拟机使用 4 vCPU、1024 MiB 内存和 8 GiB 虚拟磁盘上限；8 GiB 是逻辑容量，设备实际存储占用会随虚拟机写入增长。

安装完成后，先让电脑或其他客户端连接手机的 USB 共享网络或 Wi-Fi 热点，再访问：

```text
Web 管理： http://192.168.88.1
SSH：      ssh root@192.168.88.1
```

未在 `config.env` 设置 `OPENWRT_PASSWORD` 时，脚本会生成随机 Root 密码并只在终端显示一次；请立即保存。

## 可选配置

默认配置已经可直接安装。需要固定密码或调整资源时，安装前或安装后编辑 `config.env`，例如：

```bash
OPENWRT_PASSWORD="你的密码"
VM_CPUS="4"
VM_MEMORY_MIB="1024"
```

修改配置后执行：

```bash
bash deploy-openwrt.sh apply-config
```

该命令会上传配置并重启虚拟机。`AUTO_TAKEOVER=1` 会让 Android 本机应用的 IPv4 流量也经过 OpenWrt；默认值 `0` 更稳妥，通常无需修改。

## 日常使用

```bash
# 查看运行状态、日志和帮助
bash deploy-openwrt.sh status
bash deploy-openwrt.sh logs 200
bash deploy-openwrt.sh help

# 启动、停止或重启
bash deploy-openwrt.sh start
bash deploy-openwrt.sh stop
bash deploy-openwrt.sh restart

# 更新设备端管理脚本、辅助程序和 crosvm，并重启虚拟机
bash deploy-openwrt.sh update
```

`update` 不会替换已有的 OpenWrt 虚拟磁盘或其中的配置。

## 备份、恢复与卸载

升级、修改磁盘或卸载前先备份：

```bash
bash deploy-openwrt.sh backup
```

备份默认保存在 `backups/`，同时生成校验文件 `.sha256` 和信息文件 `.info`。恢复时必须保留备份文件及同名 `.sha256`：

```bash
bash deploy-openwrt.sh restore backups/openwrt-设备-时间.img.gz
```

卸载会删除手机中的 OpenWrt 虚拟机及其配置：

```bash
bash deploy-openwrt.sh uninstall
```

## 同步上游更新

在自己的部署分支且工作区没有未提交改动时执行：

```bash
bash tools/sync-upstream.sh
```

脚本会先与自己的 `origin` 同步，再合并上游 `upstream/main`，最后只推送回自己的 `origin`；上游远程被设置为只读，不会被误推送。发生冲突时，解决并提交冲突后重新执行该命令。

## 注意事项

- 不要直接结束 `crosvm` 进程；使用 `deploy-openwrt.sh stop`、`restart` 或 `uninstall` 管理虚拟机。
- 安装和恢复期间不要断开 ADB，也不要让手机休眠或重启。
- 默认网络模式下，只有 USB/热点客户端使用 OpenWrt；这是预期行为。
- `IPV6_PASSTHROUGH=0` 是默认值，IPv6 会经过 OpenWrt；改为 `1` 后，热点/USB 客户端的 IPv6 会绕过 OpenWrt。
