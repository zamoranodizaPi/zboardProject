## PZ7020-StarLite Nexus Sync PL control

set_property PACKAGE_PIN U18 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]
create_clock -period 20.000 -name pl_clk_50m [get_ports clk]

set_property PACKAGE_PIN G14 [get_ports user_btn]
set_property IOSTANDARD LVCMOS33 [get_ports user_btn]
set_property PULLUP true [get_ports user_btn]

set_property PACKAGE_PIN R19 [get_ports led_ok]
set_property IOSTANDARD LVCMOS33 [get_ports led_ok]

set_property PACKAGE_PIN V13 [get_ports led_fault]
set_property IOSTANDARD LVCMOS33 [get_ports led_fault]

## FakeADS SPI/DRDY on JM1 Bank35
set_property PACKAGE_PIN H16 [get_ports ads_sclk]
set_property IOSTANDARD LVCMOS33 [get_ports ads_sclk]

set_property PACKAGE_PIN H17 [get_ports ads_mosi]
set_property IOSTANDARD LVCMOS33 [get_ports ads_mosi]

set_property PACKAGE_PIN E18 [get_ports ads_miso]
set_property IOSTANDARD LVCMOS33 [get_ports ads_miso]

set_property PACKAGE_PIN E19 [get_ports ads_cs_n]
set_property IOSTANDARD LVCMOS33 [get_ports ads_cs_n]

set_property PACKAGE_PIN G17 [get_ports ads_drdy_n]
set_property IOSTANDARD LVCMOS33 [get_ports ads_drdy_n]
set_property PULLUP true [get_ports ads_drdy_n]

## Deterministic control outputs
set_property PACKAGE_PIN G18 [get_ports motor_run]
set_property IOSTANDARD LVCMOS33 [get_ports motor_run]

set_property PACKAGE_PIN D19 [get_ports field_enable]
set_property IOSTANDARD LVCMOS33 [get_ports field_enable]

set_property PACKAGE_PIN D20 [get_ports field_pwm]
set_property IOSTANDARD LVCMOS33 [get_ports field_pwm]

set_property PACKAGE_PIN J18 [get_ports sync_pulse]
set_property IOSTANDARD LVCMOS33 [get_ports sync_pulse]

set_property PACKAGE_PIN H18 [get_ports fault_out]
set_property IOSTANDARD LVCMOS33 [get_ports fault_out]

## Operator / plant inputs, pull-down by default.
set_property PACKAGE_PIN K17 [get_ports start_cmd]
set_property IOSTANDARD LVCMOS33 [get_ports start_cmd]
set_property PULLDOWN true [get_ports start_cmd]

set_property PACKAGE_PIN K18 [get_ports stop_cmd]
set_property IOSTANDARD LVCMOS33 [get_ports stop_cmd]
set_property PULLDOWN true [get_ports stop_cmd]

set_property PACKAGE_PIN L16 [get_ports reset_cmd]
set_property IOSTANDARD LVCMOS33 [get_ports reset_cmd]
set_property PULLDOWN true [get_ports reset_cmd]

set_property PACKAGE_PIN L17 [get_ports thermal_ok]
set_property IOSTANDARD LVCMOS33 [get_ports thermal_ok]
set_property PULLUP true [get_ports thermal_ok]
