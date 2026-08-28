add_test([=[IntegrationTest.SweepMultiLevelBookWithLargeCrossingOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.SweepMultiLevelBookWithLargeCrossingOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.SweepMultiLevelBookWithLargeCrossingOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:53]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.PartialFillThenCancelRemainder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.PartialFillThenCancelRemainder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.PartialFillThenCancelRemainder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:146]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.InterleavedLimitAndMarketOrders]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.InterleavedLimitAndMarketOrders]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.InterleavedLimitAndMarketOrders]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:191]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.FillBookCancelEverythingRefillAndSweep]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.FillBookCancelEverythingRefillAndSweep]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.FillBookCancelEverythingRefillAndSweep]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:274]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.SelfCrossingMultipleOrdersMatchNormally]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.SelfCrossingMultipleOrdersMatchNormally]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.SelfCrossingMultipleOrdersMatchNormally]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:334]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.BookAccessorConsistencyThroughLifecycle]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.BookAccessorConsistencyThroughLifecycle]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.BookAccessorConsistencyThroughLifecycle]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:407]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.SamePriceLevelFIFOWithCancelsAndFills]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.SamePriceLevelFIFOWithCancelsAndFills]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.SamePriceLevelFIFOWithCancelsAndFills]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:491]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[IntegrationTest.LargeScaleMarketSweepAfterPartialConsumption]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/integration_test.exe [==[--gtest_filter=IntegrationTest.LargeScaleMarketSweepAfterPartialConsumption]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegrationTest.LargeScaleMarketSweepAfterPartialConsumption]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/integration_test.cpp:539]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(integration_test_TESTS [==[IntegrationTest.SweepMultiLevelBookWithLargeCrossingOrder]==] [==[IntegrationTest.PartialFillThenCancelRemainder]==] [==[IntegrationTest.InterleavedLimitAndMarketOrders]==] [==[IntegrationTest.FillBookCancelEverythingRefillAndSweep]==] [==[IntegrationTest.SelfCrossingMultipleOrdersMatchNormally]==] [==[IntegrationTest.BookAccessorConsistencyThroughLifecycle]==] [==[IntegrationTest.SamePriceLevelFIFOWithCancelsAndFills]==] [==[IntegrationTest.LargeScaleMarketSweepAfterPartialConsumption]==])
