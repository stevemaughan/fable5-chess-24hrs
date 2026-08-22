# PGO build of the final executable.
# usage: .\source\buildfinal.ps1 [output.exe]
param([string]$Out = "final\Fable5chess24hrs.exe")

$flags = "-O3 -march=x86-64-v3 -flto -static -DNDEBUG -std=c++20"
$pgodir = "source\build\pgo"
New-Item -ItemType Directory -Force $pgodir | Out-Null
Remove-Item "$pgodir\*.gcda" -ErrorAction SilentlyContinue

# 1) instrumented build
$cmd = "g++ $flags -fprofile-generate=$pgodir -o source\build\pgo_gen.exe source\fable.cpp"
Invoke-Expression $cmd
if ($LASTEXITCODE -ne 0) { throw "instrumented build failed" }

# 2) training run: bench + a couple of timed searches
"bench`nposition startpos moves e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6`ngo movetime 3000`nposition fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1`ngo movetime 2000`nquit" | & source\build\pgo_gen.exe | Out-Null

# 3) optimized build with profile
$cmd = "g++ $flags -fprofile-use=$pgodir -fprofile-correction -o $Out source\fable.cpp"
Invoke-Expression $cmd
if ($LASTEXITCODE -ne 0) { throw "pgo build failed" }
Write-Host "PGO build written to $Out"
"bench`nquit" | & $Out
