add_test([=[LatencyRecorderTest.EmptyRecorderReturnsZero]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.EmptyRecorderReturnsZero]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.EmptyRecorderReturnsZero]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:12]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.SingleSample]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.SingleSample]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.SingleSample]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:23]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.KnownSequence1To100]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.KnownSequence1To100]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.KnownSequence1To100]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:41]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.OddCount]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.OddCount]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.OddCount]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:60]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.UnorderedInput]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.UnorderedInput]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.UnorderedInput]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:75]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.TwoSamples]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.TwoSamples]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.TwoSamples]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:91]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[LatencyRecorderTest.AllIdenticalSamples]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/latency_recorder_test.exe [==[--gtest_filter=LatencyRecorderTest.AllIdenticalSamples]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[LatencyRecorderTest.AllIdenticalSamples]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/latency_recorder_test.cpp:106]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(latency_recorder_test_TESTS [==[LatencyRecorderTest.EmptyRecorderReturnsZero]==] [==[LatencyRecorderTest.SingleSample]==] [==[LatencyRecorderTest.KnownSequence1To100]==] [==[LatencyRecorderTest.OddCount]==] [==[LatencyRecorderTest.UnorderedInput]==] [==[LatencyRecorderTest.TwoSamples]==] [==[LatencyRecorderTest.AllIdenticalSamples]==])
