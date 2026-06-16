set script_dir [file dirname [file normalize [info script]]]
set root_dir [file normalize [file join $script_dir ".."]]
set project_dir [file join $root_dir "build" "vivado"]

set project_name "pz_startlite_sync_control"
set part_name "xc7z020clg400-2"

file mkdir $project_dir
create_project -force $project_name $project_dir -part $part_name

add_files [file join $root_dir "rtl" "ads_spi_master.v"]
add_files [file join $root_dir "rtl" "zero_cross_freq.v"]
add_files [file join $root_dir "rtl" "sync_control_fsm.v"]
add_files [file join $root_dir "rtl" "pwm_gen.v"]
add_files [file join $root_dir "rtl" "pz_sync_control_top.v"]
set_property top pz_sync_control_top [current_fileset]

add_files -fileset constrs_1 [file join $root_dir "constraints" "pz_sync_control.xdc"]
update_compile_order -fileset sources_1

puts "Created project $project_name for $part_name"
