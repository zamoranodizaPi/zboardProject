// Arduino IDE entry point.
// Required firmware baseline: commit 24c2be4
// "Synchronize ADS virtual config over SPI"
// This version accepts Raspberry configuration frames over SPI/MOSI,
// uses demo-friendly 800 S/s and 10 Hz defaults, and keeps GATEDEG support.
// The actual firmware lives in ../../src/main.cpp so PlatformIO and Arduino stay aligned.
#include "../../src/main.cpp"
