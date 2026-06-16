set script_dir [file dirname [file normalize [info script]]]
set root_dir [file normalize [file join $script_dir ".."]]
set project_dir [file join $root_dir "build" "vivado"]
set project_name "pz_startlite_sync_control"

if {![file exists [file join $project_dir "$project_name.xpr"]]} {
  source [file join $script_dir "create_vivado_project.tcl"]
} else {
  open_project [file join $project_dir "$project_name.xpr"]
}

reset_run synth_1
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] != "100%"} {
  error "Synthesis did not complete"
}

launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} {
  error "Implementation/bitstream did not complete"
}

set bit_file [file join $project_dir "$project_name.runs" "impl_1" "pz_sync_control_top.bit"]
puts "Bitstream generated:"
puts $bit_file
