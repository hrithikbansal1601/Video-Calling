#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <opencv2/opencv.hpp>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <mutex>

using namespace std;

//Defined two ports one for the sender(me) and other for the receiver(other)
#define Other 5001
#define me    5000

// here pragma pack(push,1) is used so that there is no compiler generating packet such that there is no confusion for the receiver to read the packets
#pragma pack(push,1)
//udp packet structure that will be send by the sender to the receiver over udp 
struct Frame {
    uint16_t ID;  //store the packet number 
    uint32_t size;  //store the image size
    uint8_t  body[1400]; //an array of 1400 bytes such that each byte store one image pixel data 
};
#pragma pack(pop) //restores old packing

atomic<bool> Run;  //used to synchronise the updates safely  over multiple threads
vector<uchar> full_buffer;  //stores the complete incoming JPEG data //packets of 1400 bytes are stored so as so to recieve the complete frame

cv::Mat latest_frame;  //stores latest decoded frame 
mutex frame_mutex;  //used to protect latest _frame as one thread reads and other modifies so both may access it at the same time //Only ONE thread can access critical section(latest_frame) at a time

void t_send(int socket, Frame &Data, struct sockaddr* dest) {
    sendto(socket, &Data, sizeof(Data), 0, dest, sizeof(struct sockaddr_in)); //send packet to destination IP port
}

Frame t_recv(int socket, struct sockaddr_in* sender_addr) {
    Frame out;
    socklen_t addr_len = sizeof(*sender_addr);
    ssize_t n = recvfrom(socket, &out, sizeof(out), 0,(struct sockaddr*)sender_addr, &addr_len);  //receives udp packet
    // if n==-1 then no packets available
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            out.ID = 0xFFFF;  //special invalid packet
            out.size = 0;
        }
    }
    return out;

    //used non-blocking without non blocking receiver thread freezes waiting for data with non blocking receiver thread continue running
}

/*Captures video frame
compresses to jpg
split into packets
send over udp

***runs in a seperate thread***

*/
void thread_send(int socket) {
    cv::VideoCapture cap(0);  //opens the camera of your device to capture video ,open webcam
    //VideoCapture is a openCV class that is used for webcam access and video file reading 
    // 0 opens the defualt camera

    //creates a structure storing destination Ip
    struct sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(5001);  //cpu byte order to network byte order
    inet_pton(AF_INET, "10.196.41.73", &dest_addr.sin_addr);  //ip string into binary form

    while (Run) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) continue;

        cv::resize(frame, frame, cv::Size(320, 240));

        vector<uchar> buffer;//this contains the chunks to be sent, jpeg image are usually compressed raw byte so store 1 byte 
        cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY,100 });

        uint32_t total_size = buffer.size();  //total size of compressed image //encode in jpg format and stores in the buffer byte
        int chunks = (total_size + 1399) / 1400;  //no of packets required for this frame
        for (int i = 0; i < chunks; i++) {
            Frame pkt;
            pkt.ID   = i;  //used to reorder packet as packets may arrive out of order
            pkt.size = total_size;  //so that receiver can reconstruct complete image

            int offset = i * 1400;
            int bytes  = min(1400, (int)total_size - offset);

            memcpy(pkt.body, &buffer[offset], bytes);
            //delay
            t_send(socket, pkt, (struct sockaddr*)&dest_addr);
        }
    }
}

void thread_recv(int socket) {
    struct sockaddr_in sender_addr{};

    //idea is expected size 
    int expected_size = 0;
    int bytes_stored  = 0;

    while (Run) {
        Frame Z = t_recv(socket, &sender_addr);

        if (Z.ID == 0xFFFF) {
            usleep(10000);
            continue;
        }

        if (Z.ID == 0) {
            full_buffer.resize(Z.size);
            expected_size = Z.size; //Total size to be recieved.
            bytes_stored  = 0;
        }

        if (expected_size == 0) continue;

        int offset = Z.ID * 1400; //This is the data offset.
        int chunk  = min(1400, expected_size - offset); //This is the chunk number.
        if (offset + chunk <= (int)full_buffer.size()) {
            memcpy(&full_buffer[offset], Z.body, chunk);
            bytes_stored += chunk;
        }

        if (bytes_stored >= expected_size && expected_size>0) {
            cv::Mat img = cv::imdecode(full_buffer, cv::IMREAD_COLOR);

            if (!img.empty()) {
                lock_guard<mutex> lock(frame_mutex);
                latest_frame = img.clone();
            } 

            expected_size = 0;
            bytes_stored  = 0;
        }
    }

}

int main() {
    Run = true;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(me); //My Port 5000 here

    if (::bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    thread sender(thread_send, sockfd);
    thread receiver(thread_recv, sockfd);

    while (Run) {
        cv::Mat display;

        {
            lock_guard<mutex> lock(frame_mutex);
            if (!latest_frame.empty())
                display = latest_frame.clone();
        }

        if (!display.empty()) {
            cv::imshow("Receiver Output", display);
        }

        if (cv::waitKey(1) == 27) { // ESC
            Run = false;
        }
    }

    sender.join();
    receiver.join();
    return 0;
}