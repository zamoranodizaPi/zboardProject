set script_dir [file dirname [file normalize [info script]]]
set repo_dir [file normalize [file join $script_dir ".."]]
set build_dir [file join $repo_dir "build_vivado"]

create_project zboard_soft_starter $build_dir -part xc7z020clg484-1 -force

add_files -norecurse [list \
    [file join $repo_dir "rtl/sync_generator.v"] \
    [file join $repo_dir "rtl/phase_counter.v"] \
    [file join $repo_dir "rtl/control_angle.v"] \
    [file join $repo_dir "rtl/top.v"] \
    [file join $repo_dir "rtl/hardware_top.v"] \
]

add_files -fileset constrs_1 -norecurse [file join $repo_dir "constraints/zedboard.xdc"]

set_property top hardware_top [current_fileset]
update_compile_order -fileset sources_1

launch_runs synth_1 -jobs 4
wait_on_run synth_1

launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1

puts "Bitstream generated in:"
puts [file join $build_dir "zboard_soft_starter.runs/impl_1/hardware_top.bit"]
