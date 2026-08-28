add_test([=[CoreTradeTest.ConstructAndReadFields]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/core_trade_test.exe [==[--gtest_filter=CoreTradeTest.ConstructAndReadFields]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[CoreTradeTest.ConstructAndReadFields]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/core_trade_test.cpp:7]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[CoreTradeTest.MultipleTradesDistinct]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/core_trade_test.exe [==[--gtest_filter=CoreTradeTest.MultipleTradesDistinct]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[CoreTradeTest.MultipleTradesDistinct]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/core_trade_test.cpp:26]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(core_trade_test_TESTS [==[CoreTradeTest.ConstructAndReadFields]==] [==[CoreTradeTest.MultipleTradesDistinct]==])
