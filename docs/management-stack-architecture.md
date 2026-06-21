# 管理后台架构说明

## 总体结构

当前项目采用浏览器、`Rust API`、`C++ Core` 三层结构。开发板承担所有媒体与推理负载，业务接口与前端状态推送由 `seccam-api` 提供。

```text
Browser
  │
  ├── HTTP / WebSocket
  ▼
seccam-api
  │
  ├── SQLite
  └── Unix Domain Socket JSON
      ▼
seccam-core
  ├── 摄像头采集
  ├── RTSP 推流
  ├── 模型推理
  ├── 事件检测
  └── 事件录像
```

浏览器管理界面访问 `REST` 与 `WebSocket`，视频预览消费开发板 RTSP 流。`seccam-api` 可以与 `seccam-core` 部署在同一系统，也可以部署在能访问开发板 socket 的主机上。

## 目录结构

```text
core/       开发板守护进程
backend/    Rust 后端 workspace
proto/      Rust 与 C++ 共用字段模型
docs/       架构与接口说明
host_tools/ SDK、运行库与交叉编译器
```

交叉编译所需的 SDK、运行库与工具链默认放在 `host_tools/`。

## 进程职责

### seccam-core

负责所有与板端硬件和 `CVI SDK` 强绑定的功能：

1. `VI`、`ISP`、`VPSS`、`VENC`、`RTSP`
2. `TDL` 推理与目标框结果
3. 事件触发、保持时间与录像状态
4. 录像文件写入、目录扫描与容量清理
5. 当前配置、运行状态、事件列表
6. Unix Domain Socket 控制服务

### seccam-api

负责前端消费的业务接口：

1. 健康状态查询
2. 设备状态查询
3. 配置读取与更新
4. 事件列表
5. 录像列表
6. WebSocket 状态推送

`seccam-api` 不直接链接 `CVI SDK`，只通过 `UDS` 与 `seccam-core` 通信。

## IPC 约定

`seccam-api` 与 `seccam-core` 之间采用：

1. Unix Domain Socket
2. 长度前缀二进制帧
3. JSON 消息体

共享字段模型定义位于 `proto/seccam_ipc.proto`。当前构建没有 Protobuf 代码生成步骤，`.proto` 仅用于固定字段命名与结构。当前请求分为：

1. 状态查询
2. 配置查询
3. 配置更新
4. 事件列表查询
5. 录像列表查询

`WebSocket` 推送由 `seccam-api` 每秒聚合 `status`、`settings`、`events` 三类数据，并在内容变化时向前端发送新帧。

## 对外接口

当前对外接口固定为：

1. `GET /health`
2. `GET /api/v1/status`
3. `GET /api/v1/devices`
4. `GET /api/v1/settings`
5. `PATCH /api/v1/settings`
6. `GET /api/v1/events`
7. `GET /api/v1/recordings`
8. `GET /ws/status`

接口响应中的视频地址使用 `rtsp://<SECCAM_RTSP_HOST>/<stream-name>` 形式返回。

## 数据归属

### seccam-core 持有

1. 当前生效配置
2. 当前媒体状态
3. 当前检测状态
4. 当前录像状态
5. 最近事件列表

### 文件系统持有

1. 录像文件
2. `seccam-core` 配置文件
3. `seccam-api` 的 SQLite 文件

### seccam-api 持有

1. 主设备元数据
2. 对外接口地址
3. 面向前端的聚合视图

## 部署建议

### 开发板侧

运行 `seccam-core`，准备：

1. 传感器配置
2. 模型文件
3. 录像目录
4. `cvi_mpi`、`cvi_rtsp`、`cvi_tdl` 运行库

### 接口侧

运行 `seccam-api`，准备：

1. 指向 `seccam-core` 的 Unix Domain Socket
2. `SECCAM_RTSP_HOST`
3. `SECCAM_STORE_PATH`

当前实测形态为：

1. 开发板运行 `seccam-core`
2. 主机通过 `ssh -L` 转发开发板 socket
3. 主机运行 `seccam-api`
4. 浏览器访问主机上的 `REST` 与 `WebSocket`
