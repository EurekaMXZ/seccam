# seccam-core

`core/` 提供开发板侧唯一执行目标 `seccam-core`。摄像头采集、推流、推理、事件录像、状态维护和控制面都已经收敛到这个守护进程内，目标侧依赖固定为 `cvi_mpi`、`cvi_rtsp`、`cvi_tdl` 及其运行库。

## 模块划分

### daemon

`src/main.cpp`

负责装载配置、维护进程生命周期、监听 `SIGINT` 与 `SIGTERM`，并启动 Unix Domain Socket 控制服务。

### runtime

`src/config.cpp`
`src/runtime_state.cpp`
`src/types.cpp`

负责 `INI` 配置读写、运行参数快照、核心状态快照，以及检测事件和录像事件的存储。

### core_service

`src/core_service.cpp`
`src/core_backend_cvi.cpp`

负责 `seccam-core` 的主服务编排、`CVI` 原生后端创建与销毁，以及配置热更新时的后端重建。

### detection_tracker

`src/detection_tracker.cpp`

负责检测确认条件计算，维护 `trigger_hits`、`clear_misses`、目标出现状态和最近一次检测时间。

### recording_engine

`src/recording_engine.cpp`

负责事件录像、预缓冲、分段写入、容量裁剪，以及录像状态同步。

### recording_catalog

`src/recording_catalog.cpp`

负责扫描录像目录、输出录像文件列表、解析文件名中的时间戳。

### ipc

`src/ipc_server.cpp`
`src/command_dispatcher.cpp`

负责 Unix Domain Socket 服务端、长度前缀二进制帧，以及状态查询、配置查询、配置更新、事件列表、录像列表五类请求分发。

## 控制协议

共享字段模型定义位于 `proto/seccam_ipc.proto`。运行期请求与响应使用与字段模型同名的 JSON 消息体，外层为长度前缀二进制帧；当前构建没有 Protobuf 代码生成步骤。

当前公开能力包括：

1. `status_request`
2. `config_request`
3. `event_list_request`
4. `recording_list_request`

## 构建

主机侧构建只检查公共代码，不生成 `seccam-core` 可执行文件：

```bash
cmake -S . -B build/host-core
cmake --build build/host-core -j
```

目标侧构建：

```bash
./scripts/prepare_duo256m_sdk.sh
./scripts/build_board_image.sh
```

生成文件：

```text
build/duo-riscv64-release/core/seccam-core
out/seccam-milkv-duo256m-musl-riscv64-sd-v2.0.1.img.zip
```

## 运行要求

开发板部署时需要准备：

1. 传感器配置文件
2. `cvimodel` 模型文件
3. 录像目录
4. `cvi_mpi`、`cvi_rtsp`、`cvi_tdl` 运行库

当前板端实测已经覆盖：

1. 摄像头采集
2. TDL 推理
3. 人体检测事件
4. RTSP 推流
5. 事件录像
6. Unix Domain Socket 控制查询与配置更新
