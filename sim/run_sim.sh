#!/usr/bin/env bash
set -euo pipefail

mkdir -p build

iverilog -g2012 \
  -I rtl \
  -o build/soft_starter_tb.out \
  rtl/sync_generator.v \
  rtl/phase_counter.v \
  rtl/control_angle.v \
  rtl/top.v \
  tb/tb_top.v

vvp build/soft_starter_tb.out

echo
echo "VCD generated at build/waves.vcd"
echo "Run 'make wave' to open it with GTKWave when a graphical display is available."
