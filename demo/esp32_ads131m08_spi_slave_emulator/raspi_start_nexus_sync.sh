#!/usr/bin/env bash
set -u

BASE="/home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator"
LOG_DIR="/tmp"
URL="http://127.0.0.1:8092/kiosk"

mkdir -p "$LOG_DIR"

pkill -f '[r]aspi_fakefpga_web.py' 2>/dev/null || true
pkill -f '[n]exus_motor_plant_sim' 2>/dev/null || true
pkill -f '[c]hromium.*127.0.0.1:8092' 2>/dev/null || true
sleep 0.8

cd "$BASE" || exit 1

if [ -x "$BASE/raspi_motor_plant_sim/nexus_motor_plant_sim" ]; then
  nohup "$BASE/raspi_motor_plant_sim/nexus_motor_plant_sim" \
    --ads-serial /dev/ttyUSB0 \
    > "$LOG_DIR/nexus_motor_plant.log" 2>&1 < /dev/null &
else
  echo "nexus_motor_plant_sim binary not found; build it with: cd $BASE/raspi_motor_plant_sim && make" \
    > "$LOG_DIR/nexus_motor_plant.log"
fi

nohup python3 "$BASE/raspi_fakefpga_web.py" \
  --serial /dev/ttyUSB1 \
  --ads-serial /dev/ttyUSB0 \
  --port 8092 \
  > "$LOG_DIR/nexus_fakefpga_web.log" 2>&1 < /dev/null &

for _ in $(seq 1 30); do
  if curl -fsS "http://127.0.0.1:8092/api/state" >/dev/null 2>&1; then
    break
  fi
  sleep 0.5
done

if command -v chromium-browser >/dev/null 2>&1; then
  CHROME="chromium-browser"
elif command -v chromium >/dev/null 2>&1; then
  CHROME="chromium"
else
  CHROME=""
fi

if [ -n "$CHROME" ] && [ -n "${DISPLAY:-}" ]; then
  nohup "$CHROME" \
    --kiosk "$URL" \
    --noerrdialogs \
    --disable-infobars \
    --disable-session-crashed-bubble \
    --check-for-update-interval=31536000 \
    > "$LOG_DIR/nexus_chromium.log" 2>&1 < /dev/null &
else
  echo "Chromium not launched. DISPLAY=${DISPLAY:-none} CHROME=${CHROME:-none}" > "$LOG_DIR/nexus_chromium.log"
fi

echo "Nexus Sync ready: $URL"
