add_test([=[OrderTypesTest.OrderStructBasics]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_types_test.exe [==[--gtest_filter=OrderTypesTest.OrderStructBasics]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderTypesTest.OrderStructBasics]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_order_types.cpp:10]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderTypesTest.LimitOrderBasics]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_types_test.exe [==[--gtest_filter=OrderTypesTest.LimitOrderBasics]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderTypesTest.LimitOrderBasics]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_order_types.cpp:30]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderTypesTest.MarketOrderBasics]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_types_test.exe [==[--gtest_filter=OrderTypesTest.MarketOrderBasics]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderTypesTest.MarketOrderBasics]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_order_types.cpp:40]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderTypesTest.NewOrderVariantDiscrimination]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_types_test.exe [==[--gtest_filter=OrderTypesTest.NewOrderVariantDiscrimination]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderTypesTest.NewOrderVariantDiscrimination]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_order_types.cpp:50]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderTypesTest.MarketOrderHasNoPriceMember]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_types_test.exe [==[--gtest_filter=OrderTypesTest.MarketOrderHasNoPriceMember]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderTypesTest.MarketOrderHasNoPriceMember]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_order_types.cpp:85]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(order_types_test_TESTS [==[OrderTypesTest.OrderStructBasics]==] [==[OrderTypesTest.LimitOrderBasics]==] [==[OrderTypesTest.MarketOrderBasics]==] [==[OrderTypesTest.NewOrderVariantDiscrimination]==] [==[OrderTypesTest.MarketOrderHasNoPriceMember]==])
