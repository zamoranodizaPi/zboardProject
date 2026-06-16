param(
  [ValidateSet("create","build","program","build_and_program","package_axi_ip")]
  [string]$Action = "build_and_program"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Vivado = "C:\AMDDesignTools\2025.2\Vivado\bin\vivado.bat"

if (!(Test-Path $Vivado)) {
  throw "Vivado launcher not found: $Vivado"
}

switch ($Action) {
  "create" { $Tcl = Join-Path $ScriptDir "create_vivado_project.tcl" }
  "build" { $Tcl = Join-Path $ScriptDir "build_bitstream.tcl" }
  "program" { $Tcl = Join-Path $ScriptDir "program_jtag.tcl" }
  "build_and_program" { $Tcl = Join-Path $ScriptDir "build_and_program.tcl" }
  "package_axi_ip" { $Tcl = Join-Path $ScriptDir "package_axi_ip.tcl" }
}

& $Vivado -mode batch -source $Tcl
if ($LASTEXITCODE -ne 0) {
  throw "Vivado failed with exit code $LASTEXITCODE"
}
