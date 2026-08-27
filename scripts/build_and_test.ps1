$ErrorActionPreference = "Continue"
Set-Location "$PSScriptRoot\.."
cmake --build build 2>&1 | Out-File -Encoding ascii build_compile.log
ctest --test-dir build --output-on-failure -R engine_command 2>&1 | Out-File -Encoding ascii build_test.log
