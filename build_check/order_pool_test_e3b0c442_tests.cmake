add_test([=[OrderPoolTest.AcquireReturnsDistinctPointersUpToCapacity]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.AcquireReturnsDistinctPointersUpToCapacity]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.AcquireReturnsDistinctPointersUpToCapacity]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:15]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.AcquirePastCapacityReturnsNullptr]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.AcquirePastCapacityReturnsNullptr]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.AcquirePastCapacityReturnsNullptr]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:29]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.ReleaseThenAcquireReturnsSameAddress]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.ReleaseThenAcquireReturnsSameAddress]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.ReleaseThenAcquireReturnsSameAddress]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:44]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.AvailableReflectsAcquireAndRelease]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.AvailableReflectsAcquireAndRelease]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.AvailableReflectsAcquireAndRelease]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:58]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.AllPointersWithinStorageRange]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.AllPointersWithinStorageRange]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.AllPointersWithinStorageRange]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:82]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.FreeListRecyclesInLIFOOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.FreeListRecyclesInLIFOOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.FreeListRecyclesInLIFOOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:115]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.ExhaustAndFullyRecycle]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.ExhaustAndFullyRecycle]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.ExhaustAndFullyRecycle]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:134]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.ZeroCapacityPoolAlwaysReturnsNullptr]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.ZeroCapacityPoolAlwaysReturnsNullptr]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.ZeroCapacityPoolAlwaysReturnsNullptr]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:161]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.SingleSlotPool]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.SingleSlotPool]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.SingleSlotPool]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:168]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OrderPoolTest.ReleaseOutOfRangeAssertsInDebug]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_pool_test.exe [==[--gtest_filter=OrderPoolTest.DISABLED_ReleaseOutOfRangeAssertsInDebug]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OrderPoolTest.ReleaseOutOfRangeAssertsInDebug]=]
  PROPERTIES
    DISABLED YES
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/order_pool_test.cpp:186]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(order_pool_test_TESTS [==[OrderPoolTest.AcquireReturnsDistinctPointersUpToCapacity]==] [==[OrderPoolTest.AcquirePastCapacityReturnsNullptr]==] [==[OrderPoolTest.ReleaseThenAcquireReturnsSameAddress]==] [==[OrderPoolTest.AvailableReflectsAcquireAndRelease]==] [==[OrderPoolTest.AllPointersWithinStorageRange]==] [==[OrderPoolTest.FreeListRecyclesInLIFOOrder]==] [==[OrderPoolTest.ExhaustAndFullyRecycle]==] [==[OrderPoolTest.ZeroCapacityPoolAlwaysReturnsNullptr]==] [==[OrderPoolTest.SingleSlotPool]==] [==[OrderPoolTest.ReleaseOutOfRangeAssertsInDebug]==])
