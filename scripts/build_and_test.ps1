$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Set-Location "C:\Users\harsh\Desktop\MiniExchange"

Write-Output "=== BUILDING ==="
cmake --build build 2>&1 | Tee-Object -Variable buildOutput
$buildExitCode = $LASTEXITCODE
Write-Output "=== BUILD EXIT CODE: $buildExitCode ==="

if ($buildExitCode -eq 0) {
    Write-Output "=== RUNNING INTEGRATION TESTS ==="
    ctest --test-dir build --output-on-failure -R integration 2>&1
    Write-Output "=== TEST EXIT CODE: $LASTEXITCODE ==="
}
