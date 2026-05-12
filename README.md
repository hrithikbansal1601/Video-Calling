# UDP Video Calling

A real-time peer-to-peer video streaming application built in **C++** using raw **UDP sockets**, **ASIO**, and **OpenCV**.
Frames are captured from a webcam, JPEG-compressed, fragmented into fixed-size UDP packets, transmitted across the network, reassembled on the receiver side, decoded, and displayed live using a multithreaded architecture.

This project was built without external streaming frameworks such as WebRTC, GStreamer, or FFmpeg in order to understand low-level networking, packet fragmentation, concurrency, and real-time multimedia transport from scratch.

---

# Features

* Real-time webcam streaming over UDP
* Cross-platform support:

  * Linux
  * Windows
* Standalone ASIO networking
* OpenCV JPEG encoding/decoding pipeline
* Multithreaded architecture:

  * Sender thread
  * Receiver thread
  * GUI thread
* Frame fragmentation and reassembly
* Non-blocking socket handling
* Thread-safe shared frame access using `std::mutex`
* Lock-free shutdown using `atomic<bool>`
* Receive buffer tuning to reduce packet drops
* ~30 FPS sender-side frame pacing

---

# System Architecture

```text
┌─────────────────────────────────────────────────────────┐
│                        main()                           │
│    launch sender/receiver threads + GUI loop            │
│               ↕ mutex-protected latest_frame            │
├──────────────────────┬──────────────────────────────────┤
│    VideoSender       │         VideoReceiver            │
│  capture webcam      │  receive UDP packets             │
│  resize frame        │  reassemble chunks               │
│  JPEG encode         │  JPEG decode                     │
│  fragment packets    │  update shared frame             │
│  send_to()           │                                  │
└──────────────────────┴──────────────────────────────────┘
                    ↕ UDP Network
```

---

# Frame Packet Protocol

Each UDP packet uses a packed `Frame` structure:

```cpp
#pragma pack(push,1)
struct Frame {
    uint8_t body[1400];
    uint32_t size;
    uint16_t ID;
};
#pragma pack(pop)
```

`#pragma pack(push,1)` is used to avoid compiler-inserted padding bytes and ensure consistent packet layout across Windows and Linux builds.

Total packet size:

```text
1400 bytes -> payload
4 bytes    -> frame size
2 bytes    -> chunk ID
----------------------
1406 bytes total
```

| Field  | Type            | Description           |
| ------ | --------------- | --------------------- |
| `body` | `uint8_t[1400]` | Packet payload        |
| `size` | `uint32_t`      | Total JPEG frame size |
| `ID`   | `uint16_t`      | Chunk sequence index  |

---

# Fragmentation Logic

```text
chunks = ceil(total_size / 1400)

offset = ID × 1400

bytes = min(1400, remaining_bytes)
```

Frames are JPEG-encoded and fragmented into multiple UDP packets before transmission.

---

# Reassembly Logic

* `ID == 0` initializes a new frame buffer
* Incoming chunks are copied into the correct offset
* Once all bytes are received:

  * `cv::imdecode()` reconstructs the image
  * decoded frame is stored in shared memory
  * GUI thread displays frame using `cv::imshow()`

Additional safety checks are used to:

* prevent out-of-bounds writes
* reject invalid frame sizes
* avoid corrupt frame reconstruction

---

# Threading Model

| Thread          | Responsibility                                                  |
| --------------- | --------------------------------------------------------------- |
| `main`          | GUI loop and thread management                                  |
| `VideoSender`   | Camera capture, JPEG encode, fragmentation, packet transmission |
| `VideoReceiver` | UDP receive, reassembly, JPEG decode                            |

---

# Synchronisation

### Shared Frame Protection

```cpp
std::mutex frame_mutex;
```

Protects shared frame access between:

* receiver thread (writer)
* GUI thread (reader)

---

### Lock-Free Shutdown

```cpp
atomic<bool> Run;
```

Pressing `ESC` sets:

```cpp
Run = false;
```

allowing all threads to terminate cleanly.

---

# Technologies Used

* C++17
* Standalone ASIO
* OpenCV
* UDP sockets
* Multithreading (`std::thread`)
* Mutexes
* Atomics
* JPEG encoding/decoding

---

# Concepts Demonstrated

* UDP socket programming
* Cross-platform networking
* Non-blocking I/O
* Real-time multimedia streaming
* Packet fragmentation and reassembly
* Concurrent programming
* Thread synchronization
* Lock-free signalling
* OpenCV image pipelines
* JPEG compression
* Low-level systems programming

---

# Cross Platform Support

Tested on:

* Linux (Ubuntu)
* Windows (MSYS2 + MinGW)

The networking layer uses standalone ASIO for portable UDP socket handling across operating systems.

---

# Dependencies

## Ubuntu / Linux

```bash
sudo apt update
sudo apt install libopencv-dev
sudo apt install libasio-dev
```

---

## Windows (MSYS2 / MinGW)

Install:

* OpenCV
* Standalone ASIO
* MinGW-w64

Example using MSYS2:

```bash
pacman -S mingw-w64-ucrt-x86_64-opencv
pacman -S mingw-w64-ucrt-x86_64-asio
```

---

# Build

## Linux

```bash
g++ linux_main.cpp -o udp_video \
    $(pkg-config --cflags --libs opencv4) \
    -pthread \
    -std=c++17
```

---

## Windows (MSYS2 / MinGW)

```bash
g++ windows_main.cpp -o VideoCalling.exe \
    -I/ucrt64/include/opencv4 \
    -DASIO_STANDALONE \
    -lopencv_core \
    -lopencv_videoio \
    -lopencv_highgui \
    -lopencv_imgproc \
    -lopencv_imgcodecs \
    -lws2_32 \
    -lmswsock
```

---

# Usage

## 1. Edit Destination IP

Inside sender code:

```cpp
auto target_address = asio::ip::make_address("10.196.50.83");
```

Replace with the IP address of the remote machine.

---

## 2. Run on Both Machines

```bash
./udp_video
```

Each machine simultaneously acts as:

* sender
* receiver

---

## 3. Stop Application

Press:

```text
ESC
```

inside the OpenCV display window.

---

# Known Limitations

* No packet loss recovery
* No sequencing between frames
* Out-of-order packets may corrupt frames
* Fixed destination IP
* No congestion/rate control
* No encryption/authentication

---

# Future Improvements

* Frame sequence numbers
* RTP/RTCP integration
* Adaptive bitrate control
* Dynamic peer discovery
* Lightweight ACK/retransmission system
* CLI-configurable ports and IPs

---

# Project Structure

```text
udp-video-calling/
│
├── linux_main.cpp
├── windows_main.cpp
├── README.md
├── Makefile
├── .gitignore
├── LICENSE
├── UDP_Video_Calling.key
│
└── assets/
```

---

