# Builds the DeskPulse logic modules for the PC and runs the host tests.
$here = $PSScriptRoot
$src = Join-Path $here '..\DeskPulse'
$out = Join-Path $here 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$sources = @('session.cpp', 'presence.cpp', 'sound.cpp', 'buzzer.cpp', 'env.cpp', 'screen.cpp', 'storage.cpp', 'summary.cpp') |
  ForEach-Object { Join-Path $src $_ }

g++ -std=gnu++17 -Wall -Wextra -I (Join-Path $here 'shim') -I $src `
  (Join-Path $here 'test_main.cpp') @sources -o (Join-Path $out 'tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $out 'tests.exe')
exit $LASTEXITCODE
