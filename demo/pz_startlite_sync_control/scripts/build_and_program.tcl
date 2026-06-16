set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir "build_bitstream.tcl"]
source [file join $script_dir "program_jtag.tcl"]
