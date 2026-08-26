@echo off
cd /d "c:\Users\harsh\Desktop\MiniExchange"
echo Starting build...
ninja -C build benchmark_harness > build_result.txt 2>&1
echo Build exit code: %ERRORLEVEL% >> build_result.txt
echo Starting benchmark...
build\benchmark_harness.exe --benchmark_filter=NONE > bench_full_output.txt 2>&1
echo Benchmark exit code: %ERRORLEVEL% >> bench_full_output.txt
echo DONE
