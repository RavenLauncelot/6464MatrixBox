import cv2
import socket
import numpy as np
import time;

#esp32 wifi information
ESP_LOCAL_IP = "192.168.0.216"
ESP_PORT = 8888

#packet info
PANEL_WIDTH = 64
PANEL_HEIGHT = 64 
FRAME_SIZE = PANEL_WIDTH * PANEL_HEIGHT * 3
PACKETCHUNKS = 24
MAXPACKETSIZE = FRAME_SIZE // PACKETCHUNKS


videoSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

capture = cv2.VideoCapture("BadApple.mp4")

print("File opened: ", capture.isOpened())


while capture.isOpened():
    ret, frame = capture.read()

    if (not ret):
        print("no frame")
        capture = cv2.VideoCapture("C:/Users/launc/Documents/GitHub/MatrixAssistant/Code/LCDandMatrixTest/Wifi/BadApple.mp4")
        continue;
        
    frame = cv2.resize(frame, (64,64))
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)

    print(rgb)
    input()

    data = rgb.tobytes()
    #videoSocket.sendto(data, (ESP_LOCAL_IP, ESP_PORT))

    byte_cursor = 0
    
    for chunk_index in range(PACKETCHUNKS):
        # 1. Determine chunk boundaries
        chunk_start = byte_cursor
        chunk_end = MAXPACKETSIZE
        
        # Extract the chunk data
        chunk_data = data[chunk_start:chunk_end]
        
        # 2. Prepend the Chunk Index (CRITICAL for ESP32 reassembly)
        # We use a single byte for the index (0, 1, 2, ...)
        #indexed_chunk = bytes([chunk_index]) + chunk_data
        
        # 3. Send the packet
        videoSocket.sendto(chunk_data, (ESP_LOCAL_IP, ESP_PORT))
        
        # Update cursor position for the next iteration
        byte_cursor = chunk_end
        
        # OPTIONAL: Add a very small delay to prevent overwhelming the ESP32's buffer
        # This is a better alternative to time.sleep(1)
        time.sleep(0.01)
