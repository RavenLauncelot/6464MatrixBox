import socket
import cv2
import time
import queue as q
import threading
import os
import numpy as np

espPort = 8888
espIP =""

#add local ip of device i cba with some internet shit
thisIP = "192.168.0.64"

PANELWIDTH = 64
PANELHEIGHT = 64
FRAMESIZE = PANELHEIGHT * PANELWIDTH * 3

#commands are all base 8 and are sent as 3 bytes
#commands that are 1 byte in length are only sent from the matrix
#as some commands may send information 
pingCmd = bytes([0x80, 0x00, 0x00])
pongCmd = bytes([0xC0])
streamReqCmd = bytes([0x20, 0x00, 0x00])
streamReqAcceptCmd = bytes([0x30]) #when sent this cmd will contain 2 other bytes containing the chunk size
chunkReceivedCmd = bytes([0x10])

soc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    soc.bind(('', espPort))
except:
    print("Error binding port")

#currently this only works if one device is connected
#it only listen for packets of length 3. This program won't be receiving more anyway
def ListenForPackets(sock, timeout, dataReceived):
    sock.settimeout(timeout)

    try:
        data, addr = sock.recvfrom(3)
        dataReceived.put((data, addr))
        #print(data)
        #print(addr)
        dataReceived.task_done()
    except:
        print("No response, timed out... probably")

def ScanForDevice():
    responses = q.Queue()
    
    print("Starting listener thread")
    listener = threading.Thread(
        target = ListenForPackets,
        args = (soc, 5, responses)
        )
    time.sleep(1)
    listener.start()
    
    print("Scanning for devices...")
    for i in range (255):
        ipaddr = "192.168.0."+str(i)

        #don't send packet to own ip that makes things confusing
        if (ipaddr == thisIP):
            continue
            
        #print("Sending ping to: " + ipaddr);
        soc.sendto(pingCmd,(ipaddr, espPort))
        
    listener.join()

    try:
        return responses.get()
    except:
        return None
        
def SelectVideo():
    while True:
        files = None
        files = os.listdir('.')
        for file in files:
            if (file.split('.')[1] != "mp4"):
                files.remove(file)
        i = 0
        for i in range(len(files)):
            print(files[i] + " (", i+1,")")

        print("Refresh Files (" ,i+2,")")
        InputNumber = input("Input: ")
        
        try:
            #a negative 1 would be cringe
            if (int(InputNumber) < 0):
                continue
            
            selectedVideoAddr = files[int(InputNumber)-1]
            print("Selected video: ", selectedVideoAddr)
            break;
        except:
            print("Refreshing files...")
            continue

    return selectedVideoAddr

def StreamVideo(maxChunkSize, videoAddress):
    print("Sending Data packets")

    capture = cv2.VideoCapture(videoAddress)
    chunksPerFrame = int(FRAMESIZE / (maxChunkSize-1)) #rememeber their is a one byte to send that is the chunk number

    print("Chunks per Frame: ", chunksPerFrame)
    
    while capture.isOpened():
        ret, frame = capture.read()
        #ret, frame = capture.read()

        if (ret == False):
            print("Video Ended")
            break
    
        frame = cv2.resize(frame, (64,64))
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        #full frame data in bytes
        data = rgb.tobytes()

        chunkToSend = bytearray(maxChunkSize) #This is the first chunk to send so the first index will be sent as 0
        index = 0
        #now we are going to get each bit of data in the amount specified and yeah
        start = 0
        end = maxChunkSize-2 #minus 2 cus index and the data byte
        for i in range (chunksPerFrame):
            chunkToSend[0] = i

            chunkToSend[1:] = data[start:end]
            #print("Chunk indexes From: {0} To: {1}", start, end)

            soc.sendto(chunkToSend, (espIp, espPort))

            time.sleep(0.01)

            start = start + (maxChunkSize-1)
            end = end + (maxChunkSize-1)
                    
        
while True:
    scanData = ScanForDevice()

    if (scanData == None):
        print("No devices connected")
        break;
    espIp = scanData[1][0]

    
    print("Matrix connected! device IP: ", espIp)

    VideoAddr = SelectVideo()

    print("Sending stream request")

    soc.sendto(streamReqCmd, (espIp, espPort))

    streamReqResponse = None
    try:
        streamReqResponse = soc.recv(3)
    except:
        print("Error! no response, Timeout!")
        continue

    print("Packet received: " ,streamReqResponse)

    #stream request accepted cmd
    if (streamReqResponse[0] == 48):
        high = streamReqResponse[1] << 8
        low = streamReqResponse[2]
        chunkSize = high + low
        #chunkSize = np.array(chunkSize,dtype="uint16_t")
        print("Stream request accepted")
        print("Max chunk size of: ", chunkSize)

        StreamVideo(chunkSize, VideoAddr)

     
    time.sleep(0.1)
    input()

