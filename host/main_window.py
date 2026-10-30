import serial.tools.list_ports
import cv2
import numpy as np
from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, 
                               QComboBox, QPushButton, QLabel, QStackedWidget,
                               QGroupBox, QRadioButton, QButtonGroup)
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtCore import Qt, Signal
from serial_worker import WebcamWorker, ImageWorker, FRAME_SIZE

class DropLabel(QLabel):
    image_loaded = Signal(np.ndarray)

    def __init__(self):
        super().__init__("Drop Image")
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setStyleSheet("background-color: #222; color: #888; border: 2px dashed #555; border-radius: 8px; font-size: 16px;")
        self.setAcceptDrops(True)
        self.setFixedSize(480, 384)
        self.image_frame = None

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.accept()
        else:
            event.ignore()

    def dropEvent(self, event):
        urls = event.mimeData().urls()
        if urls:
            file_path = urls[0].toLocalFile()
            img = cv2.imread(file_path)
            if img is not None:
                self.image_frame = img
                self.image_loaded.emit(img)
                self.setStyleSheet("background-color: #111; color: #fff; border: 2px solid #2ecc71; border-radius: 8px;")

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("STM32 Display Manager")
        self.resize(550, 650)

        self.worker = None

        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.layout.setContentsMargins(20, 20, 20, 20)
        self.layout.setSpacing(15)

        config_group = QGroupBox("Configuration")
        config_layout = QVBoxLayout(config_group)
        
        com_row = QHBoxLayout()
        self.com_combo = QComboBox()
        self.refresh_button = QPushButton("Refresh Ports")
        com_row.addWidget(QLabel("COM Port:"))
        com_row.addWidget(self.com_combo, 1)
        com_row.addWidget(self.refresh_button)
        
        mode_row = QHBoxLayout()
        self.mode_group = QButtonGroup(self)
        self.radio_image = QRadioButton("Single Image Mode")
        self.radio_webcam = QRadioButton("Webcam Mode")
        self.radio_image.setChecked(True)
        self.mode_group.addButton(self.radio_image)
        self.mode_group.addButton(self.radio_webcam)
        mode_row.addWidget(QLabel("Mode:"))
        mode_row.addWidget(self.radio_image)
        mode_row.addWidget(self.radio_webcam)
        mode_row.addStretch()

        config_layout.addLayout(com_row)
        config_layout.addLayout(mode_row)
        self.layout.addWidget(config_group)

        self.stacked_widget = QStackedWidget()
        self.layout.addWidget(self.stacked_widget, alignment=Qt.AlignmentFlag.AlignCenter)

        self.image_widget = QWidget()
        image_layout = QVBoxLayout(self.image_widget)
        image_layout.setContentsMargins(0, 0, 0, 0)
        
        self.drop_label = DropLabel()
        self.send_image_button = QPushButton("Send Image")
        self.send_image_button.setEnabled(False)
        self.send_image_button.setMinimumHeight(40)
        
        image_layout.addWidget(self.drop_label)
        image_layout.addWidget(self.send_image_button)
        self.stacked_widget.addWidget(self.image_widget)

        self.webcam_widget = QWidget()
        webcam_layout = QVBoxLayout(self.webcam_widget)
        webcam_layout.setContentsMargins(0, 0, 0, 0)

        self.video_label = QLabel("Stream Inactive")
        self.video_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.video_label.setStyleSheet("background-color: #222; color: #888; border-radius: 8px;")
        self.video_label.setFixedSize(480, 384)
        
        webcam_controls = QHBoxLayout()
        self.toggle_stream_button = QPushButton("Start Streaming")
        self.toggle_stream_button.setMinimumHeight(40)
        webcam_controls.addWidget(self.toggle_stream_button, 1)

        transmission_stats = QHBoxLayout()
        self.fps_label = QLabel("FPS: 0.0")
        self.fps_label.setStyleSheet("font-weight: bold; color: #22cc22;")
        self.fps_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.mbps_label = QLabel("0 Mbps")
        self.mbps_label.setStyleSheet("font-weight: bold; color: #cc2222;")
        self.mbps_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        transmission_stats.addWidget(self.fps_label)
        transmission_stats.addWidget(self.mbps_label)

        webcam_layout.addWidget(self.video_label)
        webcam_layout.addLayout(webcam_controls)
        webcam_layout.addLayout(transmission_stats)
        self.stacked_widget.addWidget(self.webcam_widget)

        self.status_label = QLabel("Status: Ready")
        self.status_label.setStyleSheet("color: #777;")
        self.layout.addWidget(self.status_label)

        self.refresh_button.clicked.connect(self.populate_com_ports)
        self.radio_image.toggled.connect(self.switch_mode)
        self.drop_label.image_loaded.connect(self.on_image_dropped)
        self.send_image_button.clicked.connect(self.send_single_image)
        self.toggle_stream_button.clicked.connect(self.toggle_stream)

        self.populate_com_ports()

    def populate_com_ports(self):
        self.com_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            self.com_combo.addItem(f"{port.device} - {port.description}", port.device)

    def switch_mode(self):
        if self.worker and self.worker.isRunning():
            if isinstance(self.worker, WebcamWorker):
                self.worker.stop()
            self.worker = None
            self.toggle_stream_button.setText("Start Streaming")
            self.set_ui_enabled(True)

        if self.radio_image.isChecked():
            self.stacked_widget.setCurrentIndex(0)
        else:
            self.stacked_widget.setCurrentIndex(1)
            
        self.status_label.setText("Status: Ready")

    def set_ui_enabled(self, enabled):
        self.com_combo.setEnabled(enabled)
        self.refresh_button.setEnabled(enabled)
        self.radio_image.setEnabled(enabled)
        self.radio_webcam.setEnabled(enabled)
        self.drop_label.setAcceptDrops(enabled)

    def update_preview(self, target_label, frame_bgr):
        rgb_frame = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb_frame.shape
        qt_img = QImage(rgb_frame.data, w, h, ch * w, QImage.Format.Format_RGB888)
        pixmap = QPixmap.fromImage(qt_img).scaled(
            target_label.size(), 
            Qt.AspectRatioMode.KeepAspectRatio, 
            Qt.TransformationMode.FastTransformation
        )
        target_label.setPixmap(pixmap)

    def on_image_dropped(self, frame_bgr):
        self.update_preview(self.drop_label, frame_bgr)
        self.send_image_button.setEnabled(True)

    def send_single_image(self):
        selected_port = self.com_combo.currentData()
        if not selected_port or self.drop_label.image_frame is None:
            self.status_label.setText("Status: Missing COM port or image.")
            return

        self.set_ui_enabled(False)
        self.send_image_button.setEnabled(False)
        self.send_image_button.setText("Sending...")

        self.worker = ImageWorker(selected_port, self.drop_label.image_frame)
        self.worker.status_message.connect(self.status_label.setText)
        self.worker.finished.connect(self.on_image_sent)
        self.worker.start()

    def on_image_sent(self):
        self.set_ui_enabled(True)
        self.send_image_button.setEnabled(True)
        self.send_image_button.setText("Send Image")
        self.worker = None

    def toggle_stream(self):
        if self.worker is None or not self.worker.isRunning():
            selected_port = self.com_combo.currentData()
            if not selected_port:
                self.status_label.setText("Status: No COM port selected")
                return 

            self.worker = WebcamWorker(com_port=selected_port)
            self.worker.frame_ready.connect(lambda f: self.update_preview(self.video_label, f))
            self.worker.status_message.connect(self.status_label.setText)
            self.worker.fps_update.connect(lambda fps: self.fps_label.setText(f"FPS: {fps:.1f}"))
            self.worker.fps_update.connect(lambda fps: self.mbps_label.setText(f"{(fps * FRAME_SIZE * 8):.1f} Mbps"))
            self.worker.start()

            self.toggle_stream_button.setText("Stop Streaming")
            self.set_ui_enabled(False)
        else:
            self.worker.stop()
            self.worker = None 
            self.toggle_stream_button.setText("Start Streaming")
            self.set_ui_enabled(True)

    def closeEvent(self, event):
        if self.worker and self.worker.isRunning():
            if isinstance(self.worker, WebcamWorker):
                self.worker.stop()
        event.accept()