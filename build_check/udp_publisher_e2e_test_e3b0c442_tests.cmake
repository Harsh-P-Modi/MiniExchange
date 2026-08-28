add_test([=[PublisherE2ETest.FullScriptedSequence]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_publisher_e2e_test.exe [==[--gtest_filter=PublisherE2ETest.FullScriptedSequence]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PublisherE2ETest.FullScriptedSequence]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_publisher_e2e_test.cpp:88]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PublisherE2ETest.SequenceIsMonotonicAcrossAllMessages]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_publisher_e2e_test.exe [==[--gtest_filter=PublisherE2ETest.SequenceIsMonotonicAcrossAllMessages]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PublisherE2ETest.SequenceIsMonotonicAcrossAllMessages]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_publisher_e2e_test.cpp:172]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PublisherE2ETest.SnapshotReflectsEngineState]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_publisher_e2e_test.exe [==[--gtest_filter=PublisherE2ETest.SnapshotReflectsEngineState]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PublisherE2ETest.SnapshotReflectsEngineState]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_publisher_e2e_test.cpp:188]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(udp_publisher_e2e_test_TESTS [==[PublisherE2ETest.FullScriptedSequence]==] [==[PublisherE2ETest.SequenceIsMonotonicAcrossAllMessages]==] [==[PublisherE2ETest.SnapshotReflectsEngineState]==])
