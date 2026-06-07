// Arduino IDE entry point.
// Required firmware baseline: commit 73a74ff
// "Tune ADS demo sampling defaults"
// This version accepts Raspberry configuration frames over SPI/MOSI,
// uses demo-friendly 500 S/s and 10 Hz defaults, and keeps GATEDEG support.
// The actual firmware lives in ../../src/main.cpp so PlatformIO and Arduino stay aligned.
#include "../../src/main.cpp"
