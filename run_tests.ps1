$result = ctest --test-dir build --output-on-failure 2>&1
$result | Out-File -FilePath "test_output.txt" -Encoding ascii
