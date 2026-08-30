add_test([=[R8SpinTest.SpinSucceedsOnceSpaceFreed]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/r8_spin_test.exe [==[--gtest_filter=R8SpinTest.SpinSucceedsOnceSpaceFreed]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[R8SpinTest.SpinSucceedsOnceSpaceFreed]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/r8_spin_test.cpp:19]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(r8_spin_test_TESTS [==[R8SpinTest.SpinSucceedsOnceSpaceFreed]==])
