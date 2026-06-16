set script_dir [file dirname [file normalize [info script]]]
set root_dir [file normalize [file join $script_dir ".."]]
set ip_root [file join $root_dir "build" "ip_repo" "nexus_sync_control_1_0"]
set tmp_project_dir [file join $root_dir "build" "ip_packager_tmp"]

file delete -force $ip_root
file delete -force $tmp_project_dir
file mkdir $ip_root
file mkdir $tmp_project_dir

create_project -force nexus_sync_ip_packager $tmp_project_dir -part xc7z020clg400-2

foreach src {
  ads_spi_master.v
  zero_cross_freq.v
  sync_control_fsm.v
  pwm_gen.v
  sync_control_axi_regs.v
  pz_sync_control_axi_top.v
} {
  add_files [file join $root_dir "rtl" $src]
}
set_property top pz_sync_control_axi_top [current_fileset]
update_compile_order -fileset sources_1

ipx::package_project \
  -root_dir $ip_root \
  -vendor nexus.local \
  -library user \
  -taxonomy /Nexus \
  -import_files \
  -set_current true

set core [ipx::current_core]
set_property name nexus_sync_control $core
set_property display_name "Nexus Sync Control AXI" $core
set_property description "FakeADS SPI acquisition, synchronous motor control FSM, and AXI-Lite telemetry registers." $core
set_property version 1.0 $core
set_property supported_families {zynq Production} $core

set_property model_name pz_sync_control_axi_top [ipx::get_file_groups xilinx_anylanguagesynthesis -of_objects $core]
set_property model_name pz_sync_control_axi_top [ipx::get_file_groups xilinx_anylanguagebehavioralsimulation -of_objects $core]

set s_axi [ipx::get_bus_interfaces s_axi -of_objects $core]
set_property interface_mode slave $s_axi
set_property abstraction_type_vlnv xilinx.com:interface:aximm_rtl:1.0 $s_axi
set_property bus_type_vlnv xilinx.com:interface:aximm:1.0 $s_axi
set_property slave_memory_map_ref s_axi $s_axi

set mem_map [ipx::add_memory_map s_axi $core]
set addr_block [ipx::add_address_block reg0 $mem_map]
set_property range 4096 $addr_block
set_property width 32 $addr_block
set_property usage register $addr_block

ipx::associate_bus_interfaces -busif s_axi -clock s_axi_aclk $core
set_property driver_value 0 [ipx::get_ports ads_cs_n -of_objects $core]
set_property driver_value 0 [ipx::get_ports ads_sclk -of_objects $core]
set_property driver_value 0 [ipx::get_ports ads_mosi -of_objects $core]
set_property driver_value 0 [ipx::get_ports motor_run -of_objects $core]
set_property driver_value 0 [ipx::get_ports field_enable -of_objects $core]
set_property driver_value 0 [ipx::get_ports field_pwm -of_objects $core]
set_property driver_value 0 [ipx::get_ports sync_pulse -of_objects $core]
set_property driver_value 0 [ipx::get_ports fault_out -of_objects $core]

ipx::update_checksums $core
ipx::save_core $core
puts "Packaged Nexus Sync AXI IP:"
puts $ip_root
