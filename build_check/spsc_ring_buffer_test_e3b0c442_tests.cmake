add_test([=[SpscRingBufferTest.EmptyOnConstruction]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.EmptyOnConstruction]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.EmptyOnConstruction]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:13]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.TryPopOnEmptyReturnsFalse]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.TryPopOnEmptyReturnsFalse]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.TryPopOnEmptyReturnsFalse]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:19]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.PushUpToCapacityThenFull]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.PushUpToCapacityThenFull]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.PushUpToCapacityThenFull]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:26]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.FifoOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.FifoOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.FifoOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:37]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.WrapAround]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.WrapAround]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.WrapAround]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:56]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.InterleavedPushPop]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.InterleavedPushPop]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.InterleavedPushPop]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:79]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.CapacityAccessor]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.CapacityAccessor]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.CapacityAccessor]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:91]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.WorksWithEngineCommand]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.WorksWithEngineCommand]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.WorksWithEngineCommand]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:97]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SpscRingBufferTest.MoveSemantics]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/spsc_ring_buffer_test.exe [==[--gtest_filter=SpscRingBufferTest.MoveSemantics]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SpscRingBufferTest.MoveSemantics]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/spsc_ring_buffer_test.cpp:126]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(spsc_ring_buffer_test_TESTS [==[SpscRingBufferTest.EmptyOnConstruction]==] [==[SpscRingBufferTest.TryPopOnEmptyReturnsFalse]==] [==[SpscRingBufferTest.PushUpToCapacityThenFull]==] [==[SpscRingBufferTest.FifoOrder]==] [==[SpscRingBufferTest.WrapAround]==] [==[SpscRingBufferTest.InterleavedPushPop]==] [==[SpscRingBufferTest.CapacityAccessor]==] [==[SpscRingBufferTest.WorksWithEngineCommand]==] [==[SpscRingBufferTest.MoveSemantics]==])
