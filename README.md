# RoboMaster 2026 自定义客户端 (门锁狙击手)

这是一个为 RoboMaster 2026 赛季设计的轻量级、纯 Python 编写的操作手自定义客户端。
它的核心功能是通过大疆裁判系统自定义数据链路（`0x0310`）或本地串口，接收由车端由于带宽和协议限制进行高度碎片化（290 Bytes）的 H.264 裸流碎片，并实时拼装、解码显示。画面上叠加了狙击手专用的自定义准星 UI。

## 🎯 核心特性
- **双模通信**：支持线下直接插串口排线硬连调试（`serial`模式），也支持赛场标准 UDP 链路（`udp`模式）。
- **极简跨平台**：完全抛弃 ROS，使用 PyAV（FFmpeg 包装）基于 Python 和硬件解码，无缝适配操作手使用的 Windows 11 主机。
- **并行渲染架构**：采用 IO 请求与 UI 渲染相解耦的多线程队列，完美避免由 OpenCV(QT) 导致的 Linux/Windows 串口粘包及段错误。
- **自定义 UI**：内置等比例放大、准星偏移、截图抓取等实战功能。

---

## 🛠️ 安装环境 (Windows 11)

请确保你的操作手电脑已经安装了 `Python 3.10` 或更高版本。

1. **进入目录并创建虚拟环境**
打开 PowerShell （按 `Win + R` 键输入 `powershell`）：
```powershell
cd 你的路径\gxu_rm_custom_client
python -m venv venv
```

2. **激活虚拟环境并安装依赖**
```powershell
.\venv\Scripts\activate
pip install -r requirements.txt
```
*(依赖包含: av, opencv-python, numpy, pyserial)*

---

## 🚀 启动指引

### 1. 线下修车/直连调试 (串口模式)
当使用 USB转TTL 模块将操作手电脑与车端 STM32/Orin 物理连接时使用：
```powershell
python client_main.py --mode serial --dev COM3 --baud 115200
```
> **注意**：Windows 下通常为 `COM3`, `COM4` 等；Linux 下通常为 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。

### 2. 正式比赛/全链路测试 (UDP模式)
实战中，配合大疆官方操作手客户端的 UDP 转发口使用：
```powershell
python client_main.py --mode udp --port 10000
```
> **注意**：对应的大疆官方客户端需要配置相同的 UDP 开放端口。

### 3. 可选启动参数控制

- `--mode` : 选择通信模式，`serial` 或 `udp`
- `--port` : UDP 模式下的监听端口
- `--dev` : 串口模式下的设备号（如 Windows 的 COM3 或 Linux 的 /dev/ttyACM0）
- `--baud` : 串口模式下的波特率，建议至少 921600 以防丢包
- `--width` 和 `--height` : 输入的原始图像分辨率，默认为 640x480
- `--display_scale` : UI界面放大倍数，默认为 2
- `--crosshair_offset_x` 和 `--crosshair_offset_y` : 准星的 X/Y 轴偏移量，默认为 0
- `--crosshair_width` : 准星线条宽度，默认为 2
- `--debug_dump_enable` : 是否开启调试保存图像功能，默认为 False
- `--debug_dump_every_n_frames` : 调试模式下每多少帧保存一次截图
- `--debug_dump_dir` : 调试截图保存目录，默认为 `debug_dumps`
- `--help` : 查看所有参数说明

---

## 📦 给电控/上下位机通信协议的特别说明

### 1. 图传链路包结构
从上位机发送到 STM32，再到客户端的单包必须绝对遵循以下格式，总长 **294 字节**：
| 字节 0~1 | 字节 2~291 | 字节 292~293 |
| :---: | :---: | :---: |
| `0x53` `0x56` (SV) | `290 Bytes H.264 Payload` | `CRC16` 校验码 |

### 2. 🩸 致命避坑：STM32 USB 串口丢包问题 (大于 64 字节)
USB CDC (虚拟串口) 在 FS (全速) 模式下的硬件底层**每次最大的批量传输包（Max Packet Size）只有 64 字节**。
上位机下发 294 字节时，底层会被自动且极速地劈成：`64 + 64 + 64 + 64 + 38`。

**电控团队必须注意：**
绝对**不能**在 `main` 的 `while(1)` 中去轮询拷贝接收区代码，第一块 64 字节一旦没取走，1ms内就会被第二块 64 字节覆盖导致永远丢包！
**解决办法**：在 STM32 的 `usbd_cdc_if.c` 的 `CDC_Receive_FS()` 接收中断回调函数中，手工实现一个 **Ring Buffer (环形队列)**。硬件中断收到任何数据立马推入队列，主循环再去队列拿数据拼够 294 字节。

### 3. 波特率建议
为防止裁判系统通道抢占，请在上位机配置文件与 STM32 虚拟串口代码中，尽量将波特率从默认的 `115200` 提升至 `921600` 或更高。