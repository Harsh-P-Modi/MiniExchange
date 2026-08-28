add_test([=[PoolExhaustionTest.ThirdOrderRejectedWhenPoolFull]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.ThirdOrderRejectedWhenPoolFull]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.ThirdOrderRejectedWhenPoolFull]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:47]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.RejectedOrderIdNotRecorded]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.RejectedOrderIdNotRecorded]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.RejectedOrderIdNotRecorded]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:68]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.NoEventsEmittedOnRejection]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.NoEventsEmittedOnRejection]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.NoEventsEmittedOnRejection]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:92]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.BookUnchangedAfterRejection]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.BookUnchangedAfterRejection]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.BookUnchangedAfterRejection]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:115]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.CancelFreesSlotAndNextAddSucceeds]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.CancelFreesSlotAndNextAddSucceeds]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.CancelFreesSlotAndNextAddSucceeds]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:136]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.FullFillFreesSlotForNextOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.FullFillFreesSlotForNextOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.FullFillFreesSlotForNextOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:162]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PoolExhaustionTest.MarketOrderNotBlockedByPoolExhaustion]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/order_book_test.exe [==[--gtest_filter=PoolExhaustionTest.MarketOrderNotBlockedByPoolExhaustion]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PoolExhaustionTest.MarketOrderNotBlockedByPoolExhaustion]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/pool_exhaustion_test.cpp:185]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(order_book_test_TESTS [==[PoolExhaustionTest.ThirdOrderRejectedWhenPoolFull]==] [==[PoolExhaustionTest.RejectedOrderIdNotRecorded]==] [==[PoolExhaustionTest.NoEventsEmittedOnRejection]==] [==[PoolExhaustionTest.BookUnchangedAfterRejection]==] [==[PoolExhaustionTest.CancelFreesSlotAndNextAddSucceeds]==] [==[PoolExhaustionTest.FullFillFreesSlotForNextOrder]==] [==[PoolExhaustionTest.MarketOrderNotBlockedByPoolExhaustion]==])
