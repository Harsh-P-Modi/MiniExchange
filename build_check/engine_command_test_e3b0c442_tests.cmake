add_test([=[EngineCommandTest.HoldsLimitOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/engine_command_test.exe [==[--gtest_filter=EngineCommandTest.HoldsLimitOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EngineCommandTest.HoldsLimitOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/engine_command_test.cpp:9]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EngineCommandTest.HoldsMarketOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/engine_command_test.exe [==[--gtest_filter=EngineCommandTest.HoldsMarketOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EngineCommandTest.HoldsMarketOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/engine_command_test.cpp:24]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EngineCommandTest.HoldsCancelRequest]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/engine_command_test.exe [==[--gtest_filter=EngineCommandTest.HoldsCancelRequest]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EngineCommandTest.HoldsCancelRequest]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/engine_command_test.cpp:38]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EngineCommandTest.VisitDispatchesCorrectly]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/engine_command_test.exe [==[--gtest_filter=EngineCommandTest.VisitDispatchesCorrectly]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EngineCommandTest.VisitDispatchesCorrectly]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/engine_command_test.cpp:50]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(engine_command_test_TESTS [==[EngineCommandTest.HoldsLimitOrder]==] [==[EngineCommandTest.HoldsMarketOrder]==] [==[EngineCommandTest.HoldsCancelRequest]==] [==[EngineCommandTest.VisitDispatchesCorrectly]==])
