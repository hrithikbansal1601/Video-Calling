# UDP Video Calling

A real-time peer-to-peer video streaming application built in C++ using raw UDP sockets and OpenCV. Frames are captured, JPEG-compressed, fragmented into 1400-byte UDP packets, transmitted, reassembled on the receiver side, and displayed live — all with a multithreaded architecture and no external streaming libraries.

---

## Features

- **Custom 1406-byte frame protocol** over non-blocking UDP sockets
- **Multithreaded design** — separate Send, Receive, and GUI threads
- **JPEG video pipeline** — 320×240 @ quality 100, ~40 KB per frame, fewer than 30 chunks per frame
- **Mutex-protected shared state** using `std::mutex` + `lock_guard`
- **Lock-free shutdown** via `atomic<bool> Run` triggered by ESC key
- **Cross-host communication** — tested across two machines on a local network

---

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        main()                           │
│   socket() → bind() → launch threads → imshow loop     │
│               ↕ shared fd, mutex-protected latest_frame │
├──────────────────────┬──────────────────────────────────┤
│    thread_send       │         thread_recv              │
│  cap >> frame        │  recvfrom() packets              │
│  resize 320×240      │  reassemble chunks               │
│  JPEG encode         │  imdecode()                      │
│  fragment 1400B      │  { lock } latest_frame = img     │
│  sendto()            │                                  │
└──────────────────────┴──────────────────────────────────┘
          ↕  UDP Network  10.196.41.73:5001
```

---

## Frame Packet Protocol

Each UDP packet is a fixed-size `Frame` struct (1406 bytes, packed with `#pragma pack(push,1)`):

| Field    | Type       | Size   | Description                        |
|----------|------------|--------|------------------------------------|
| `ID`     | `uint16_t` | 2 B    | Chunk index (0-based); `0xFFFF` = timeout sentinel |
| `size`   | `uint32_t` | 4 B    | Total JPEG frame size in bytes     |
| `body`   | `uint8_t[]`| 1400 B | Payload bytes for this chunk       |

**Fragmentation:**
```
chunks  = ceil(total_size / 1400)
offset  = ID × 1400
bytes   = min(1400, remaining)
```

**Reassembly:** `ID == 0` resets state and pre-allocates `full_buffer` to `size` bytes. `0xFFFF` (EAGAIN/EWOULDBLOCK) causes a 10 ms sleep and retry.

---

## Threading Model

| Thread | Responsibilities |
|---|---|
| **main** | Creates socket, binds to port 5000, launches workers, runs `cv::imshow` / `waitKey` loop (OpenCV GUI must stay on the main thread), joins on exit |
| **thread_send** | Captures frames from camera, resizes, JPEG-encodes, fragments, and sends via `sendto()` |
| **thread_recv** | Receives packets via `recvfrom()`, reassembles into a full JPEG, decodes with `cv::imdecode`, and writes to `latest_frame` under lock |

**Synchronisation primitives:**
- `std::mutex` + `lock_guard` — protects `latest_frame` between the receiver writer and the GUI reader
- `atomic<bool> Run` — lock-free shutdown signal; set to `false` on ESC, causing both worker threads to exit cleanly

---

## Socket Design

```cpp
// UDP socket
int fd = socket(AF_INET, SOCK_DGRAM, 0);

// Non-blocking I/O
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

// Bind to port 5000 (receiver)
bind(fd, &addr, sizeof(addr));

// Send / Receive
sendto(fd, &pkt, ...);
recvfrom(fd, &pkt, ...);
```

- **UDP** — low latency, connectionless. Frame loss is tolerable; delay is not.
- **Non-blocking** — `EAGAIN`/`EWOULDBLOCK` returns immediately; receiver thread sleeps 10 ms and retries.
- **One socket, two threads** — send and receive share the same `fd`. Distinct ports (5000 local, 5001 remote) prevent self-loopback.

---

## Prerequisites

- Linux (tested on Ubuntu)
- `g++` with C++11 or later
- [OpenCV](https://opencv.org/) (`libopencv-dev`)
- A working webcam

---

## Build

```bash
g++ main.cpp -o udp_video \
    $(pkg-config --cflags --libs opencv4) \
    -lpthread -std=c++17
```

---

## Usage

1. **Edit the destination IP** in `thread_send()`:
   ```cpp
   inet_pton(AF_INET, "10.196.41.73", &dest_addr.sin_addr);
   ```
   Replace with the IP address of the remote machine.

2. **Run on both machines** (each acts as sender and receiver simultaneously):
   ```bash
   ./udp_video
   ```

3. **Stop** by pressing **ESC** in the display window.

> Ports used: `5000` (local bind) and `5001` (remote destination). Ensure these are open in any firewall.

---

## Known Limitations & Future Work

### Current Limitations
- No packet loss handling — missing chunks produce corrupt frames
- No sequence numbers — interleaved packets from different frames are silently dropped
- Fixed destination IP — no dynamic peer discovery
- Sender can flood the link with no rate control

### Planned Improvements
- **Frame IDs** — detect stale or out-of-order chunks
- **RTP/RTCP** — adopt standard real-time transport protocol
- **Sender-side 30 fps rate cap** — avoid link saturation
- **CLI args** — configure IP, ports, and quality at runtime
- **Lightweight ACK** — basic loss detection

---

## Project Structure

```
.
├── main.cpp          # Full source (send thread, recv thread, main GUI loop)
└── README.md
```

---

## License

This project is provided for educational purposes.
