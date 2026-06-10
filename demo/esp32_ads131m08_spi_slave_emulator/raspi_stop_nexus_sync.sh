#!/usr/bin/env bash

pkill -f '[r]aspi_fakefpga_web.py' 2>/dev/null || true
pkill -f '[n]exus_motor_plant_sim' 2>/dev/null || true
pkill -f '[c]hromium.*127.0.0.1:8092' 2>/dev/null || true

echo "Nexus Sync stopped"
