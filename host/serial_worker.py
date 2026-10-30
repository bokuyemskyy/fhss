import cv2
import numpy as np
import serial
import time
from PySide6.QtCore import QThread, Signal
from collections import deque

DISPLAY_WIDTH  = 160
DISPLAY_HEIGHT = 128
PIXEL_FORMAT   = "RGB565"
FRAME_SIZE     = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2

STATS_SAMPLE_PERIOD = 10

def convert_frame(frame): 
    b, g, r = cv2.split(frame)

    if PIXEL_FORMAT == "RGB332": 
        r_3bit = (r >> 5) & 0x07 
        g_3bit = (g >> 5) & 0x07 
        b_2bit = (b >> 6) & 0x03 
        rgb332 = ( (r_3bit << 5) | (g_3bit << 2) | b_2bit ) 
        pixel_bytes = rgb332.astype(np.uint8).tobytes() 
        preview_bgr = cv2.merge([ (b_2bit * 255 // 3), (g_3bit * 255 // 7), (r_3bit * 255 // 7) ]).astype(np.uint8) 
        return pixel_bytes, preview_bgr 

    elif PIXEL_FORMAT == "RGB565": 
        r_5bit = (r >> 3) & 0x1F 
        g_6bit = (g >> 2) & 0x3F 
        b_5bit = (b >> 3) & 0x1F 
        rgb565 = ( (r_5bit.astype(np.uint16) << 11) | (g_6bit.astype(np.uint16) << 5) | b_5bit.astype(np.uint16) ) 
        pixel_bytes = rgb565.astype(">u2").tobytes() 
        preview_bgr = cv2.merge([b & 0xF8, g & 0xFC, r & 0xF8])
        return pixel_bytes, preview_bgr

class WebcamWorker(QThread):
    frame_ready = Signal(np.ndarray)
    status_message = Signal(str)
    fps_update = Signal(float)
    mbps_update = Signal(float)

    def __init__(self, com_port, baudrate=115200):
        super().__init__()
        self.com_port = com_port
        self.baudrate = baudrate
        self.is_running = True
        self.frame_times = deque(maxlen=STATS_SAMPLE_PERIOD)
        self.mbits_sent = deque(maxlen=STATS_SAMPLE_PERIOD)

    def run(self):
        try:
            port = serial.Serial(self.com_port, self.baudrate, timeout=1.0, write_timeout=2.0)
            port.reset_output_buffer()
        except Exception as e:
            self.status_message.emit(f"Serial error: {e}")
            return

        camera = cv2.VideoCapture(0)
        if not camera.isOpened():
            self.status_message.emit("Error: Could not open camera")
            port.close()
            return

        camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        prev_time = time.perf_counter()
        self.fps_update.emit(0.0)

        while self.is_running:
            ret, frame = camera.read()
            if not ret: continue

            resized = cv2.resize(frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT))
            pixel_bytes, preview_bgr = convert_frame(resized)
            assert len(pixel_bytes) == FRAME_SIZE, "frame size must match STM32 FRAME_SIZE"

            self.frame_ready.emit(preview_bgr)

            curr_time = time.perf_counter()
            time_diff = curr_time - prev_time
            prev_time = curr_time
            self.frame_times.append(time_diff)

            avg_diff = sum(self.frame_times) / len(self.frame_times)
            if avg_diff > 0:
                self.fps_update.emit(1.0 / avg_diff)

            try:
                port.write(pixel_bytes)

            except serial.SerialException as e:
                self.status_message.emit(f"Serial error: {e}")
                break

        camera.release()
        port.close()
        self.fps_update.emit(0.0)
        self.status_message.emit("Webcam stopped.")

    def stop(self):
        self.is_running = False
        self.wait()


class ImageWorker(QThread):
    status_message = Signal(str)
    finished = Signal()

    def __init__(self, com_port, image_frame, baudrate=115200):
        super().__init__()
        self.com_port = com_port
        self.image_frame = image_frame
        self.baudrate = baudrate

    def run(self):
        try:
            self.status_message.emit("Connecting...")
            port = serial.Serial(self.com_port, self.baudrate, timeout=1.0, write_timeout=10.0)
            port.reset_output_buffer()
        except Exception as e:
            self.status_message.emit(f"Serial error: {e}")
            self.finished.emit()
            return

        try:
            self.status_message.emit("Processing and sending image...")
            resized = cv2.resize(self.image_frame, (DISPLAY_WIDTH, DISPLAY_HEIGHT))
            pixel_bytes, _ = convert_frame(resized)
            port.write(pixel_bytes)
            port.flush()
            self.status_message.emit("Image sent successfully.")
        except Exception as e:
            self.status_message.emit(f"Serial error: {e}")
        finally:
            port.close()
            self.finished.emit()