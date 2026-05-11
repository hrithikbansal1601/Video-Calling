CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread
OPENCV = $(shell pkg-config --cflags --libs opencv4)

TARGET = udp_video
SRC = main.cpp

all:
	$(CXX) $(SRC) -o $(TARGET) $(CXXFLAGS) $(OPENCV)

clean:
	rm -f $(TARGET)