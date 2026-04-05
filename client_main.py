import cv2
import av
import argparse
import socket
import serial
import threading
import queue
from pathlib import Path
import struct
import time

class RMH264Decoder:
    def __init__(self):
        self.codec = av.CodecContext.create('h264', 'r')
        self.codec.thread_type = 'FRAME'
        if hasattr(av.codec.context.Flags, 'LOW_DELAY'):
            self.codec.flags |= av.codec.context.Flags.LOW_DELAY
        elif hasattr(av.codec.context.Flags, 'low_delay'):
            self.codec.flags |= av.codec.context.Flags.low_delay
            
    def decode(self, packet_bytes: bytes):
        bgr_frames = []
        try:
            packets = self.codec.parse(packet_bytes)
            for pkt in packets:
                frames = self.codec.decode(pkt)
                for frame in frames:
                    img = frame.to_ndarray(format='bgr24')
                    bgr_frames.append(img)
        except Exception as e:
            pass
        return bgr_frames

class CustomClientGUI:
    def __init__(self, args):
        self.args = args
        self.decoder = RMH264Decoder()
        
        # 显示参数
        self.display_width = args.width * args.display_scale
        self.display_height = args.height * args.display_scale
        
        # 调试保存
        self.debug_dump_enable = args.debug_dump_enable
        self.debug_dump_dir = Path(args.debug_dump_dir)
        self.display_frame_counter = 0
        if self.debug_dump_enable:
            self.debug_dump_dir.mkdir(parents=True, exist_ok=True)
            
        # UI队列
        self.frame_queue = queue.Queue(maxsize=3)
        self.running = True

    def _draw_overlay(self, img):
        h, w = img.shape[:2]
        cx = max(0, min(w - 1, w // 2 + self.args.crosshair_offset_x))
        cy = max(0, min(h - 1, h // 2 + self.args.crosshair_offset_y))

        crosshair_color = (230, 190, 235)
        cv2.line(img, (0, cy), (w - 1, cy), crosshair_color, self.args.crosshair_width, cv2.LINE_AA)
        cv2.line(img, (cx, 0), (cx, h - 1), crosshair_color, self.args.crosshair_width, cv2.LINE_AA)

        center_color = (170, 255, 170)
        center = (w // 2, h // 2)
        cv2.circle(img, center, 24, center_color, 1, cv2.LINE_AA)
        
        cv2.putText(img, "CUSTOM CLIENT ALIVE", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

    def start_ui_loop(self):
        # 【重要修复】OpenCV(Qt) 规定所有的窗口渲染和操作只能在操作系统的“主线程”执行！
        window_name = 'RoboMaster Sniper HUD'
        cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(window_name, self.display_width, self.display_height)
        
        while self.running:
            try:
                img = self.frame_queue.get(timeout=0.05)
                # 只有成功获取非空画面才处理
                if img is not None and img.size > 0:
                    img_disp = cv2.resize(
                        img, 
                        (self.display_width, self.display_height), 
                        interpolation=cv2.INTER_NEAREST
                    )
                    self._draw_overlay(img_disp)
                    cv2.imshow(window_name, img_disp)
                    
                    if self.debug_dump_enable:
                        self.display_frame_counter += 1
                        if self.display_frame_counter % self.args.debug_dump_every_n_frames == 0:
                            save_path = self.debug_dump_dir / f'frame_{self.display_frame_counter:08d}.png'
                            cv2.imwrite(str(save_path), img_disp)
                            print(f"[Debug] Saved frame to {save_path}")
            except queue.Empty:
                pass
                
            if cv2.waitKey(1) & 0xFF == ord('q'):
                self.running = False
                break
                
        cv2.destroyAllWindows()

    def process_payload(self, data: bytes):
        frames = self.decoder.decode(data)
        for img in frames:
            try:
                self.frame_queue.put_nowait(img)
            except queue.Full:
                pass


def io_worker(gui, args):
    if args.mode == 'serial':
        print(f"[*] 监听串口 {args.dev} @ {args.baud}bps ...")
        try:
            ser = serial.Serial(args.dev, args.baud, timeout=0.1)
        except Exception as e:
            print(f"[!] 串口打开失败: {e}")
            gui.running = False
            return
            
        buffer = bytearray()
        PACKET_SIZE = 294
        
        while gui.running:
            if ser.in_waiting > 0:
                buffer.extend(ser.read(ser.in_waiting))
                while len(buffer) >= PACKET_SIZE:
                    if buffer[0] == 0x53 and buffer[1] == 0x56:
                        packet = buffer[:PACKET_SIZE]
                        payload = packet[2:292]
                        gui.process_payload(payload)
                        buffer = buffer[PACKET_SIZE:]
                    else:
                        buffer.pop(0)
            else:
                time.sleep(0.005)
                
    elif args.mode == 'udp':
        print(f"[*] 监听 UDP: {args.port} ...")
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("0.0.0.0", args.port))
            sock.settimeout(0.1)
        except Exception as e:
            print(f"[!] UDP 绑定失败: {e}")
            gui.running = False
            return
            
        while gui.running:
            try:
                data, addr = sock.recvfrom(2048)
                # 假设比赛时UDP中拿到的是裸流载荷
                gui.process_payload(data)  
            except socket.timeout:
                continue

def main():
    parser = argparse.ArgumentParser(description="RM Custom Client Video Decoder")
    parser.add_argument('--mode', type=str, choices=['udp', 'serial'], default='serial',
                        help='接收模式 (udp网络 或 serial串口)')
    parser.add_argument('--port', type=int, default=10000, help='UDP 监听端口')
    parser.add_argument('--dev', type=str, default='COM3', help='串口设备号 (如 COM3 或 /dev/ttyACM0)')
    parser.add_argument('--baud', type=int, default=115200, help='串口波特率')
    
    parser.add_argument('--width', type=int, default=400, help='原始图像宽度')
    parser.add_argument('--height', type=int, default=400, help='原始图像高度')
    parser.add_argument('--display_scale', type=int, default=2, help='UI界面放大倍数')
    parser.add_argument('--crosshair_offset_x', type=int, default=0, help='准星X轴偏移')
    parser.add_argument('--crosshair_offset_y', type=int, default=0, help='准星Y轴偏移')
    parser.add_argument('--crosshair_width', type=int, default=2, help='准星线条宽度')
    parser.add_argument('--debug_dump_enable', action='store_true', help='是否开启调试保存图像功能')
    parser.add_argument('--debug_dump_every_n_frames', type=int, default=20, help='调试模式下每多少帧保存一次截图')
    parser.add_argument('--debug_dump_dir', type=str, default='sniper_debug_imgs', help='调试截图保存目录')
    
    args = parser.parse_args()
    gui = CustomClientGUI(args)

    # 【修复段错误的核心】：数据读取放到子线程，GUI 渲染放在主线程
    t = threading.Thread(target=io_worker, args=(gui, args), daemon=True)
    t.start()

    # 主线程接管 GUI 显示
    gui.start_ui_loop()

if __name__ == '__main__':
    main()
