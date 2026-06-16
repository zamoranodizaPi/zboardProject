param()

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $Here "scripts\run_vivado.ps1") -Action package_axi_ip
