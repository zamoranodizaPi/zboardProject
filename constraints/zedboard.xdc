## ZedBoard Zynq-7000 XC7Z020CLG484-1
## Safe LED/button hardware test. No AC power or SCR driver is connected here.

set_property PACKAGE_PIN Y9 [get_ports clk_100mhz]
set_property IOSTANDARD LVCMOS33 [get_ports clk_100mhz]
create_clock -period 10.000 -name clk_100mhz [get_ports clk_100mhz]

set_property PACKAGE_PIN T18 [get_ports btn_up]
set_property IOSTANDARD LVCMOS33 [get_ports btn_up]

set_property PACKAGE_PIN R16 [get_ports btn_down]
set_property IOSTANDARD LVCMOS33 [get_ports btn_down]

set_property PACKAGE_PIN P16 [get_ports btn_reset]
set_property IOSTANDARD LVCMOS33 [get_ports btn_reset]

set_property PACKAGE_PIN T22 [get_ports led_sync]
set_property IOSTANDARD LVCMOS33 [get_ports led_sync]

set_property PACKAGE_PIN T21 [get_ports led_gate]
set_property IOSTANDARD LVCMOS33 [get_ports led_gate]

set_property PACKAGE_PIN U22 [get_ports led_zc]
set_property IOSTANDARD LVCMOS33 [get_ports led_zc]

set_property PACKAGE_PIN U21 [get_ports {led_angle[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led_angle[0]}]

set_property PACKAGE_PIN V22 [get_ports {led_angle[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led_angle[1]}]

set_property PACKAGE_PIN W22 [get_ports {led_angle[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led_angle[2]}]

set_property PACKAGE_PIN U19 [get_ports {led_angle[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led_angle[3]}]

set_property PACKAGE_PIN U14 [get_ports {led_angle[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led_angle[4]}]
