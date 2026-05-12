#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <asio.hpp> 
#include <opencv2/opencv.hpp>

using namespace std;
using asio::ip::udp; 

// Port configuration
constexpr unsigned short MY_PORT = 5001;
constexpr unsigned short OTHER_PORT = 5000;


//pragma is used for inter os consistency over the network
#pragma pack(push, 1)
struct Frame {
    uint8_t body[1400];    // Payload data 
    uint32_t size;         // Total size of the complete frame
    uint16_t ID;           // Sequence number of this chunk
};
#pragma pack(pop)
//TOTAL SIZE IS 1406 BYTES.

atomic<bool> Run(true);
cv::Mat last_frame;
mutex frame_mutex; //to print
vector<uchar> full_buffer; //current values that it recieved.
class VideoSender {
private:
    asio::io_context& io_context_;
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    cv::VideoCapture cap_;
    
public:
    
    VideoSender(asio::io_context& io_context)
    : io_context_(io_context),
    socket_(io_context, udp::endpoint(udp::v4(), OTHER_PORT)),
cap_(0)
{
    auto target_address = asio::ip::make_address("10.196.50.83");
    remote_endpoint_ = udp::endpoint(target_address, 5000);
    if (!cap_.isOpened()) {
        perror("The Camera is Not Working");
    }
}
    void start_sending() {
        while (Run) {
            cv::Mat frame;
            cap_ >> frame;
            
            if (frame.empty()) {
                this_thread::sleep_for(chrono::milliseconds(10)); //wait for the nect frame
                continue;
            }
            
            cv::resize(frame, frame, cv::Size(320, 240));
            vector<uchar> buffer;
            cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 100});
            
            uint32_t total_size = buffer.size();//total size
            int num_chunks = (total_size + 1399) / 1400; //number of chunks it is broken into.
            
            for (int i = 0; i < num_chunks; i++) {
                Frame packet;
                packet.ID = i;
                packet.size = total_size;
                
                int offset = i * 1400;
                int bytes_in_chunk = min(1400, (int)total_size - offset);
                memcpy(packet.body, &buffer[offset], bytes_in_chunk);
                
                try {
                    socket_.send_to(
                        asio::buffer(&packet, sizeof(Frame)),
                        remote_endpoint_
                    );
                } catch (const asio::system_error& e) {
                    cerr << "Send error: " << e.what() << endl;
                }
            }
            this_thread::sleep_for(chrono::milliseconds(33)); // ~30 FPS
        }
    }
};

class VideoReceiver {
private:
    asio::io_context& io_context_;
    udp::socket socket_;
    udp::endpoint sender_endpoint_; //sending address.
    uint32_t expected_size_ = 0;
    uint32_t bytes_received_ = 0;

public:
    VideoReceiver(asio::io_context& io_context)
    : io_context_(io_context),socket_(io_context, udp::endpoint(udp::v4(), 5001)) 
{
    asio::socket_base::receive_buffer_size option(2 * 1024 * 1024); //to send the recieve buffer size so data is not dropped .
    socket_.set_option(option);
    socket_.non_blocking(true);
}

    void start_receiving() {
        Frame received_packet;
        while (Run) {
            try {
                size_t bytes_recvd = socket_.receive_from(
                    asio::buffer(&received_packet, sizeof(Frame)),
                    sender_endpoint_
                );
                if (bytes_recvd > 0) {
                    if (received_packet.ID == 0) {
                        if (received_packet.size > 1000 && received_packet.size < 1000000) {
                            full_buffer.assign(received_packet.size, 0);
                            expected_size_ = received_packet.size;
                            bytes_received_ = 0;
                        } else {
                            expected_size_ = 0; // Ignore garbage frame
                            return; 
                        }
                    }

                    if (expected_size_ > 0) {
                        int offset = (int)received_packet.ID * 1400;
                        
                        // LOGIC: Calculate exactly how many bytes are in this specific packet.
                        // It may be different for the last chunk of the frame.
                        int bytes_to_copy = 1400;
                        if (offset + 1400 > (int)expected_size_) {
                            bytes_to_copy = (int)expected_size_ - offset;
                        }

                        // prevent "Extraneous Bytes"
                        if (offset >= 0 && (offset + bytes_to_copy) <= (int)full_buffer.size()) {
                            memcpy(full_buffer.data() + offset, received_packet.body, bytes_to_copy);
                            bytes_received_ += bytes_to_copy; //to findd  the extra bits.
                        } else {
                            //Somehow extra bytes were sent.
                            cerr << "[ERROR] Write out of bounds! ID: " << received_packet.ID << endl;
                        }
                    }
                    if (bytes_received_ >= expected_size_ && expected_size_ > 0) {
                        cv::Mat img = cv::imdecode(full_buffer, cv::IMREAD_COLOR);

                        if (!img.empty()) {
                            lock_guard<mutex> lock(frame_mutex);
                            last_frame = img.clone(); 
                        }
                        
                        // Reset for the next frame
                        expected_size_ = 0;
                        bytes_received_ = 0;
                    }
                }
            } catch (const asio::system_error& e) {
                // Logic: would_block is normal for non-blocking sockets when no data is ready
                if (e.code() != asio::error::would_block) {
                    cerr << "Recv Error: " << e.what() << endl;
                }
                this_thread::sleep_for(chrono::milliseconds(5));
            }
        }
    }
};
int main() {

    try {
        asio::io_context io_context;
        VideoSender sender(io_context);
        VideoReceiver receiver(io_context);

        thread send_thread([&]() { sender.start_sending(); });
        thread recv_thread([&]() { receiver.start_receiving(); });


        while (Run) {
            cv::Mat display;
            {
                lock_guard<mutex> lock(frame_mutex);
                if (!last_frame.empty()) display = last_frame.clone();
            }

            if (!display.empty()) {
                cv::imshow("P2P Video", display);
            }
            
            // Logic: waitKey(1) is the ONLY way OpenCV processes GUI events
            if (cv::waitKey(1) == 27) Run = false;
        }

        Run = false;
        send_thread.join();
        recv_thread.join();
    } catch (const exception& e) {
        cerr << "[FATAL] " << e.what() << endl;
    }
    return 0;
}

//g++ Test2.cpp -o VideoCalling.exe -I/ucrt64/include/opencv4 -DASIO_STANDALONE -lopencv_core -lopencv_videoio -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs -lws2_32 -lmswsock