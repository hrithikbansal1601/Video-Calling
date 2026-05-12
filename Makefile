CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread

OPENCV = $(shell pkg-config --cflags --libs opencv4)

LINUX_TARGET = udp_video
WINDOWS_TARGET = VideoCalling.exe

all: linux

linux:
	$(CXX) linux_main.cpp -o $(LINUX_TARGET) \
	$(CXXFLAGS) $(OPENCV)

windows:
	$(CXX) windows_main.cpp -o $(WINDOWS_TARGET) \
	-DASIO_STANDALONE \
	-lopencv_core \
	-lopencv_videoio \
	-lopencv_highgui \
	-lopencv_imgproc \
	-lopencv_imgcodecs \
	-lws2_32 \
	-lmswsock

clean:
	rm -f $(LINUX_TARGET) $(WINDOWS_TARGET)