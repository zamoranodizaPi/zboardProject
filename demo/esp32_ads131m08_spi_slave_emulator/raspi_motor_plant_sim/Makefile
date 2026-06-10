TARGET := nexus_motor_plant_sim
SRC := nexus_motor_plant_sim.cpp

PKG_CONFIG ?= pkg-config
GPIOD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libgpiod 2>/dev/null)
GPIOD_LIBS := $(shell $(PKG_CONFIG) --libs libgpiod 2>/dev/null)

CXX ?= g++
CXXFLAGS ?= -O2 -pipe -Wall -Wextra -std=c++17 -pthread
LDFLAGS ?= -pthread

ifneq ($(GPIOD_LIBS),)
  CXXFLAGS += -DUSE_GPIOD $(GPIOD_CFLAGS)
  LDFLAGS += $(GPIOD_LIBS)
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
