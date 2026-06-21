# seccam

`seccam` 是面向 Milk-V Duo 256M 的端侧安防系统。当前仓库已经收敛为一套生产形态的实现：

1. 开发板运行 `C++ seccam-core`，负责摄像头采集、推流、推理、事件录像与本地控制面
2. `Rust seccam-api` 负责前端所需的 `REST` 与 `WebSocket` 接口
3. 浏览器界面只访问 `seccam-api`，视频预览消费开发板 RTSP 流

共享字段模型定义保留在 `proto/`，运行期传输采用 Unix Domain Socket 上的长度前缀二进制帧与 JSON 消息体。当前构建没有 Protobuf 代码生成步骤。架构说明见 `docs/management-stack-architecture.md`。

## 仓库结构

```text
core/       开发板守护进程
backend/    Rust 后端 workspace
proto/      IPC 字段模型
docs/       架构与接口说明
host_tools/ SDK、交叉编译器、板端运行库
scripts/    SDK 准备、交叉编译、部署与远端启动脚本
```

## 最终部署形态

浏览器侧访问：

```text
HTTP:      http://<backend-host>:8080
WebSocket: ws://<backend-host>:8080/ws/status
RTSP:      rtsp://<board-host>/<stream-name>
```

进程分工：

```text
Browser
  │
  ├── HTTP / WebSocket
  ▼
seccam-api
  │
  ├── SQLite 设备索引
  └── UDS JSON 控制
      ▼
seccam-core
  ├── VI / ISP / VPSS / VENC / RTSP
  ├── TDL 推理
  ├── 检测事件
  └── 录像文件
```

当前仓库以开发板同机部署为主：`seccam-core` 与 `seccam-api` 一并写入官方 Duo 256M 系统镜像，开机后由 `/mnt/system/auto.sh` 拉起。

## 主机准备

```bash
sudo apt-get update
sudo apt-get install -y \
  git build-essential cmake make pkg-config \
  wget curl unzip xz-utils file \
  rsync docker.io
```

## 构建板镜像

本地生成可烧录镜像：

```bash
docker build -f docker/board-image-builder.Dockerfile -t seccam-board-builder docker
docker run --rm --privileged \
  -v "$PWD:/workspace" \
  -w /workspace \
  seccam-board-builder \
  ./scripts/build_board_image.sh
```

生成文件：

```text
out/seccam-milkv-duo256m-musl-riscv64-sd-v2.0.1.img
out/seccam-milkv-duo256m-musl-riscv64-sd-v2.0.1.img.zip
out/seccam-milkv-duo256m-musl-riscv64-sd-v2.0.1.img.zip.sha256
```

GitHub Actions 入口位于 `.github/workflows/build-board-image.yml`，只负责生成板端 `.img` 产物。

## 单独构建二进制

准备交叉编译资源：

```bash
./scripts/prepare_duo256m_sdk.sh
```

`seccam-core` 依赖官方镜像中的运行库；首次单独编译前，先执行一次 `build_board_image.sh`，让脚本从官方镜像提取运行库到 `build/runtime/official-v2`。随后可以单独构建：

```bash
./scripts/build_core_riscv64.sh
./scripts/build_api_riscv64.sh
```

生成文件：

```text
build/duo-riscv64-release/core/seccam-core
backend/target/riscv64gc-unknown-linux-musl/release/seccam-api
```

## 开发板内文件布局

```text
/mnt/system/auto.sh
/mnt/data/seccam/bin/seccam-core
/mnt/data/seccam/bin/seccam-api
/mnt/data/seccam/bin/start-seccam.sh
/mnt/data/seccam/seccam-core.ini
/mnt/data/seccam/seccam-api.env
/mnt/data/seccam/recordings/
```

## 启动 seccam-api

开发板同机部署时，`seccam-api` 由 `/mnt/data/seccam/bin/start-seccam.sh` 拉起。主机侧调试时，也可以单独运行：

```bash
cd backend
cargo build --workspace
SECCAM_BACKEND_ADDR=0.0.0.0:8080 \
SECCAM_CORE_SOCKET=/var/run/seccam-core.sock \
SECCAM_RTSP_HOST=192.168.42.1 \
cargo run -p seccam-api
```

常用环境变量：

```text
SECCAM_BACKEND_ADDR   HTTP 监听地址
SECCAM_CORE_SOCKET    seccam-core 的 Unix Domain Socket
SECCAM_RTSP_HOST      返回给前端的 RTSP 主机名或地址
SECCAM_STORE_PATH     SQLite 文件
SECCAM_DEVICE_ID      设备编号
SECCAM_DEVICE_NAME    设备名称
```

## 前端接口

当前接口前缀为 `/api/v1`：

```text
GET   /health
GET   /api/v1/status
GET   /api/v1/devices
GET   /api/v1/settings
PATCH /api/v1/settings
GET   /api/v1/events?limit=<n>
GET   /api/v1/recordings?limit=<n>&newer_than_ms=<ms>
GET   /ws/status
```

`/ws/status` 每秒聚合推送一次状态、配置与最近事件；内容无变化时保持静默。

## 录像与预览

录像文件当前保存为原始 `H.264 Annex B` 码流，开发板侧编码结果会按事件切分写入录像目录。浏览器管理端通过 `REST` 与 `WebSocket` 获取控制与状态信息，视频预览使用开发板 RTSP 流。
