# RoboMaster 2026 自定义客户端 (C++/Qt6版)

这是一个为 RoboMaster 2026 赛季设计的 C++ 编写的操作手自定义客户端。
它的核心功能是通过 MQTT 接收和发送 Protobuf 序列化的比赛数据，同时通过 UDP 接收并实时解码 H.265 (HEVC) 图传码流，并在其上叠加自定义战术 UI。

## 🎯 核心特性
- **多线程强实时架构**：UI 渲染、MQTT 通信、UDP 视频流接收与 FFmpeg 解码完全在独立线程（`QThread`）中运行，告别界面卡顿。
- **H.265/HEVC 高效解码**：使用 FFmpeg (`libavcodec`, `av_parser_parse2`) 对 UDP 3334 端口接收的自定义分片 NALU 流进行组包与解码。
- **MQTT + Protobuf 通信**：原生集成 `mosquitto` 与 `protobuf`，全量支持比赛状态数据（如 `GameStatus`）和控制指令（如 `KeyboardMouseControl`, `MapClickInfoNotify`）的双向高速收发。
- **Qt6 现代化 UI**：全自绘界面，支持自定义悬浮准星、状态仪表盘、小地图点击等交互。

## 🛠️ 环境依赖 (Ubuntu)

请确保你的开发机器已安装下列基础库项：

```bash
sudo apt update
# 安装 Qt6
sudo apt install -y qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
# 安装 FFmpeg (解码与颜色转换)
sudo apt install -y libavcodec-dev libavutil-dev libswscale-dev
# 安装 Mosquitto (MQTT 通信)
sudo apt install -y libmosquitto-dev mosquitto
# 安装 Protobuf (序列化)
sudo apt install -y protobuf-compiler libprotobuf-dev
# 构建工具
sudo apt install -y cmake g++ make
```

## 🚀 编译与启动指引

1. **进入目录进行 CMake 构建**
```bash
mkdir -p build && cd build
cmake ..
make -j16
```

2. **运行客户端**
```bash
./RmCustomClient
```

## 📦 架构与协议特别说明

### 1. 图传链路包包头结构 (UDP 3334)
客户端在 `3334` 端口监听图传碎片，拼装格式为 H.265 (HEVC)。
每个 UDP 数据包的前 8 个字节已被定义为分片校验头：
- `[0-1]`：帧编号 (2 Byte, 递增)
- `[2-3]`：分片序号 (2 Byte, 递增)
- `[4-7]`：当前帧总字节数 (4 Byte)
- `[8+]` ：裸 H.265 码流碎片

### 2. MQTT 通信协议
- **代理服务器地址**：默认为 `127.0.0.1:3333`（由车端路由或如 SharkDataSever 下发提供）。
- 消息数据层采用 Protobuf 直接搭载在二进制负载上。
- **频次注意**：客户端上行的 `KeyboardMouseControl` 等高频控制指令依据操作手设备刷新率触发。
