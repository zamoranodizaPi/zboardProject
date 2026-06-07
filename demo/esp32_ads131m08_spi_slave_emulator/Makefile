CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

TARGET := raspi_native_scope
SRC := raspi_native_scope.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
