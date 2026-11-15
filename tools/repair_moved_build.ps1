# repair_moved_build.ps1 - make a CMake build folder work again after its project was moved or renamed.
#
# CMake keeps absolute paths in CMakeCache.txt; once the folder moves, configure and build refuse
# ("The current CMakeCache.txt directory ... is different than the directory ... where CMakeCache.txt
# was created"). This reads where the cache was made, finds the part of the path that changed (the
# trailing folders both paths share did not move), replaces the old part with the new one in the cache,
# and removes the generated build files (CMakeFiles/, ninja state). Every setting in the cache is kept.
# Nothing happens when the folder has not moved, so callers can run it before every build.
# Built programs do not contain these paths; only the next build recompiles everything once (the
# compile commands, which ninja compares, now contain the new paths).
#
#   repair_moved_build.ps1 -Build <build folder> [-Configure] [-Cmake <cmake.exe>]
#     -Configure   also run the cmake configure step now (output in <build>\reconfigure_log.txt);
#                  without it the caller's next configure / build does that.
# Exit code 0 = not moved / repaired, 1 = configure failed. One line is printed when it repairs.
param(
    [Parameter(Mandatory = $true)][string]$Build,
    [switch]$Configure,
    [string]$Cmake = 'C:\msys64\mingw64\bin\cmake.exe'
)
$ErrorActionPreference = 'Stop'
$here = [IO.Path]::GetFullPath($Build).TrimEnd('\', '/')
$cache = Join-Path $here 'CMakeCache.txt'
if (!(Test-Path -LiteralPath $cache)) { exit 0 }
$line = Select-String -LiteralPath $cache -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$' | Select-Object -First 1
if (!$line) { exit 0 }
$was = $line.Matches[0].Groups[1].Value.Trim().TrimEnd('\', '/').Replace('\', '/')
$now = $here.Replace('\', '/')
if ($was -ieq $now) { exit 0 }

# the trailing folders both paths share did not move: old root -> new root is the rest
$a = $was.Split('/'); $b = $now.Split('/')
$n = 0
while ($n -lt $a.Count - 1 -and $n -lt $b.Count - 1 -and $a[$a.Count - 1 - $n] -ieq $b[$b.Count - 1 - $n]) { $n++ }
$oldRoot = ($a[0..($a.Count - 1 - $n)] -join '/')
$newRoot = ($b[0..($b.Count - 1 - $n)] -join '/')

$text = [IO.File]::ReadAllText($cache)
$fixed = $text
foreach ($pair in @(@($oldRoot, $newRoot), @($oldRoot.Replace('/', '\'), $newRoot.Replace('/', '\')))) {
    # whole path parts only: "...\Remake" must not match "...\Remake2"
    $fixed = [regex]::Replace($fixed, [regex]::Escape($pair[0]) + '(?=[/\\;"\r\n]|$)', $pair[1].Replace('$', '$$'),
                              [Text.RegularExpressions.RegexOptions]'IgnoreCase, Multiline')
}
[IO.File]::WriteAllText($cache, $fixed)
foreach ($f in @('CMakeFiles', 'build.ninja', 'rules.ninja', '.ninja_log', '.ninja_deps', 'cmake_install.cmake')) {
    $p = Join-Path $here $f
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
}
Write-Host "Build folder moved, cache repaired (it recompiles once): $here"

if ($Configure) {
    $src = (Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1).Matches[0].Groups[1].Value
    $env:PATH = [IO.Path]::GetDirectoryName($Cmake) + ';' + $env:PATH
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & $Cmake -S $src -B $here *> (Join-Path $here 'reconfigure_log.txt')
    $code = $LASTEXITCODE; $ErrorActionPreference = $prev
    if ($code -ne 0) { Write-Host "   configure FAILED: see $here\reconfigure_log.txt" -ForegroundColor Red; exit 1 }
}
exit 0
