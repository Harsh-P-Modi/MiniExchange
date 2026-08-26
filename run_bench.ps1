Set-Location "c:\Users\harsh\Desktop\MiniExchange"
& ".\build\benchmark_harness.exe" --benchmark_filter=NONE 2>&1 | Out-File -FilePath "bench_full_output.txt" -Encoding utf8
Write-Output "BENCHMARK_COMPLETE"
