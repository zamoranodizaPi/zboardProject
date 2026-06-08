CXX ?= g++
CXXFLAGS ?= -O3 -pipe -Wall -Wextra -std=c++17 -pthread
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

TARGET := fakefpga_monitor
SRC := fakefpga_monitor.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

clean:
	rm -f $(TARGET)
