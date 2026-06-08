CXX ?= g++
PKG_CONFIG ?= pkg-config

TARGET := ads131_scope_sdl
SRC := ads131_scope_sdl.cpp

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)

CXXFLAGS ?= -O3 -pipe -Wall -Wextra -std=c++17 -pthread $(SDL_CFLAGS)
LDFLAGS ?= -pthread $(SDL_LIBS) -latomic

.PHONY: all clean install-service

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

install-service:
	sudo install -m 0755 $(TARGET) /usr/local/bin/ads131_scope_sdl
	sudo install -m 0644 ads131-scope.service /etc/systemd/system/ads131-scope.service
	sudo systemctl daemon-reload
