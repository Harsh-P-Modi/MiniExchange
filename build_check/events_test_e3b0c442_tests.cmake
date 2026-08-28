add_test([=[EventsTest.EngineResultEnumValues]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.EngineResultEnumValues]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.EngineResultEnumValues]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:11]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EventsTest.EngineResponseConstruction]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.EngineResponseConstruction]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.EngineResponseConstruction]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:29]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EventsTest.EngineResponseEmptyTrades]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.EngineResponseEmptyTrades]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.EngineResponseEmptyTrades]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:54]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EventsTest.OrderAcceptedConstruction]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.OrderAcceptedConstruction]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.OrderAcceptedConstruction]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:67]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EventsTest.OrderCancelledConstruction]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.OrderCancelledConstruction]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.OrderCancelledConstruction]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:82]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[EventsTest.MultipleTradesInResponse]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/events_test.exe [==[--gtest_filter=EventsTest.MultipleTradesInResponse]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[EventsTest.MultipleTradesInResponse]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_events.cpp:97]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(events_test_TESTS [==[EventsTest.EngineResultEnumValues]==] [==[EventsTest.EngineResponseConstruction]==] [==[EventsTest.EngineResponseEmptyTrades]==] [==[EventsTest.OrderAcceptedConstruction]==] [==[EventsTest.OrderCancelledConstruction]==] [==[EventsTest.MultipleTradesInResponse]==])
