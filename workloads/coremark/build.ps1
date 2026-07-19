#Requires -Version 5.1
<#
.SYNOPSIS
  Build CoreMark for the RV32IM OoO core and emit imem/dmem hex.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Iterations 1 -OutDir ..\..\rtl_v2\tb\workloads_hex
#>
param(
  [int]$Iterations = 1,
  [string]$OutDir = "",
  [string]$Toolchain = "",
  [string]$Name = "coremark"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $Root "..\..")).Path
$Upstream = Join-Path $Root "upstream"
$Port = Join-Path $Root "port"
$Build = Join-Path $Root "build"

if (-not $OutDir) {
  # ooo_rtl tree uses rtl_v2/tb; github package uses tb/
  $cand = @(
    (Join-Path $RepoRoot "tb\workloads_hex"),
    (Join-Path $RepoRoot "rtl_v2\tb\workloads_hex")
  )
  $OutDir = ($cand | Where-Object { Test-Path (Split-Path $_ -Parent) } | Select-Object -First 1)
  if (-not $OutDir) { $OutDir = $cand[0] }
}
if (-not $Toolchain) {
  $Toolchain = Join-Path $RepoRoot "tools\riscv-toolchain\xpack-riscv-none-elf-gcc-14.2.0-3\bin"
  if (-not (Test-Path (Join-Path $Toolchain "riscv-none-elf-gcc.exe"))) {
    $alt = Join-Path $RepoRoot "..\ooo_rtl\tools\riscv-toolchain\xpack-riscv-none-elf-gcc-14.2.0-3\bin"
    if (Test-Path (Join-Path $alt "riscv-none-elf-gcc.exe")) { $Toolchain = $alt }
  }
}

$gcc = Join-Path $Toolchain "riscv-none-elf-gcc.exe"
$objdump = Join-Path $Toolchain "riscv-none-elf-objdump.exe"
if (-not (Test-Path $gcc)) {
  throw "RISC-V GCC not found at $gcc. Install xPack riscv-none-elf-gcc under tools/riscv-toolchain/."
}
if (-not (Test-Path (Join-Path $Upstream "core_main.c"))) {
  throw "CoreMark sources missing. Run: git clone --depth 1 https://github.com/eembc/coremark.git `"$Upstream`""
}

New-Item -ItemType Directory -Force -Path $Build | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$inc = @("-I$Port", "-I$Upstream")
$defs = @(
  "-DITERATIONS=$Iterations",
  "-DPERFORMANCE_RUN=1",
  "-DTOTAL_DATA_SIZE=2000",
  "-DHAS_FLOAT=0",
  "-DMEM_METHOD=MEM_STATIC",
  "-DMAIN_HAS_NOARGC=1"
)
$cflags = @(
  "-march=rv32im", "-mabi=ilp32", "-mcmodel=medlow",
  "-Os", "-ffunction-sections", "-fdata-sections",
  "-ffreestanding", "-fno-builtin", "-fno-common",
  "-nostdlib", "-Wall", "-Wno-unused"
) + $inc + $defs

$srcs = @(
  (Join-Path $Upstream "core_list_join.c"),
  (Join-Path $Upstream "core_main.c"),
  (Join-Path $Upstream "core_matrix.c"),
  (Join-Path $Upstream "core_state.c"),
  (Join-Path $Upstream "core_util.c"),
  (Join-Path $Port "core_portme.c"),
  (Join-Path $Port "ee_printf.c"),
  (Join-Path $Port "crt0.S")
)

$objs = @()
foreach ($src in $srcs) {
  $obj = Join-Path $Build ((Split-Path $src -Leaf) + ".o")
  & $gcc @cflags -c $src -o $obj
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
  $objs += $obj
}

$elf = Join-Path $Build "$Name.elf"
$ld = Join-Path $Port "link.ld"
$map = Join-Path $Build "$Name.map"
$linkArgs = @("-T", $ld, "-Wl,--gc-sections", "-Wl,-Map=$map") + $objs + @("-o", $elf, "-lgcc")
& $gcc @cflags @linkArgs
if ($LASTEXITCODE -ne 0) { throw "link failed" }

& $objdump -h $elf | Out-Host
& $objdump -t $elf | Select-String -Pattern "(_start|main|g_coremark|_stack|_bss|_etext)" | Out-Host

$elfToHex = Join-Path $RepoRoot "tools\elf_to_hex.py"
if (-not (Test-Path $elfToHex)) {
  $elfToHex = Join-Path $RepoRoot "..\ooo_rtl\tools\elf_to_hex.py"
}
python $elfToHex --elf $elf --out-dir $OutDir --name $Name
if ($LASTEXITCODE -ne 0) { throw "elf_to_hex failed" }

$man = Join-Path $OutDir "manifest.csv"
$imemRel = "$Name.imem.hex"
$dmemRel = "$Name.dmem.hex"
$imemWords = (Get-Content (Join-Path $OutDir $imemRel)).Count
$dmemWords = (Get-Content (Join-Path $OutDir $dmemRel)).Count
$row = "$Name,$elf,$imemRel,$dmemRel,$imemWords,$dmemWords"

if (Test-Path $man) {
  $lines = Get-Content $man
  $hdr = $lines[0]
  $rest = @($lines | Select-Object -Skip 1 | Where-Object { $_ -and ($_ -notmatch "^$Name,") })
  Set-Content -Path $man -Value (@($hdr) + $rest + $row)
} else {
  Set-Content -Path $man -Value @("name,source,imem,dmem,text_words,data_words", $row)
}

Write-Host ""
Write-Host "Built $Name (ITERATIONS=$Iterations)"
Write-Host "  ELF : $elf"
Write-Host "  HEX : $(Join-Path $OutDir $imemRel) / $dmemRel"
Write-Host "  IMEM words: $imemWords  DMEM words: $dmemWords"
if ($imemWords -gt 4096) {
  Write-Warning "IMEM image ($imemWords words) exceeds default IMEM_DEPTH=4096. Bump pkg_cpu::IMEM_DEPTH before simulating."
}
Write-Host "Run: .\build\pipeline_sim.exe --quiet --csv $Name --max-cycles 5000000 --mem-system cached --imem tb\workloads_hex\$imemRel --dmem tb\workloads_hex\$dmemRel"
