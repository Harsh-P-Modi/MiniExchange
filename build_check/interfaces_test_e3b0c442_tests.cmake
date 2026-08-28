add_test([=[NullEventSinkTest.SingletonReturnsSamePointer]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.SingletonReturnsSamePointer]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.SingletonReturnsSamePointer]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:35]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[NullEventSinkTest.SingletonIsNotNull]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.SingletonIsNotNull]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.SingletonIsNotNull]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:41]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[NullEventSinkTest.OnTradeDoesNotCrash]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.OnTradeDoesNotCrash]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.OnTradeDoesNotCrash]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:45]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[NullEventSinkTest.OnOrderAcceptedDoesNotCrash]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.OnOrderAcceptedDoesNotCrash]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.OnOrderAcceptedDoesNotCrash]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:58]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[NullEventSinkTest.OnOrderCancelledDoesNotCrash]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.OnOrderCancelledDoesNotCrash]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.OnOrderCancelledDoesNotCrash]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:63]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[NullEventSinkTest.PolymorphicDispatchAllMethods]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=NullEventSinkTest.PolymorphicDispatchAllMethods]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[NullEventSinkTest.PolymorphicDispatchAllMethods]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:70]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[RecordingEventSinkTest.CountsEachEventType]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/interfaces_test.exe [==[--gtest_filter=RecordingEventSinkTest.CountsEachEventType]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[RecordingEventSinkTest.CountsEachEventType]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/test_interfaces.cpp:102]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(interfaces_test_TESTS [==[NullEventSinkTest.SingletonReturnsSamePointer]==] [==[NullEventSinkTest.SingletonIsNotNull]==] [==[NullEventSinkTest.OnTradeDoesNotCrash]==] [==[NullEventSinkTest.OnOrderAcceptedDoesNotCrash]==] [==[NullEventSinkTest.OnOrderCancelledDoesNotCrash]==] [==[NullEventSinkTest.PolymorphicDispatchAllMethods]==] [==[RecordingEventSinkTest.CountsEachEventType]==])
