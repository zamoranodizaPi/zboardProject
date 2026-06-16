set script_dir [file dirname [file normalize [info script]]]
set root_dir [file normalize [file join $script_dir ".."]]
set project_dir [file join $root_dir "build" "vivado"]
set bit_file [file join $project_dir "pz_startlite_sync_control.runs" "impl_1" "pz_sync_control_top.bit"]

if {![file exists $bit_file]} {
  error "Bitstream not found. Run scripts/build_bitstream.tcl first. Missing: $bit_file"
}

open_hw_manager
connect_hw_server
open_hw_target
set devs [get_hw_devices]
puts "Detected HW devices:"
puts $devs

set zynq_dev ""
foreach dev $devs {
  if {[string match "*xc7z020*" $dev] || [string match "*7z020*" $dev] || [string match "*xc7z*" $dev]} {
    set zynq_dev $dev
    break
  }
}
if {$zynq_dev eq ""} {
  error "No Zynq device found over JTAG."
}

current_hw_device $zynq_dev
refresh_hw_device -update_hw_probes false $zynq_dev
set_property PROGRAM.FILE $bit_file $zynq_dev
program_hw_devices $zynq_dev
refresh_hw_device $zynq_dev
puts "Programmed $zynq_dev with $bit_file"
