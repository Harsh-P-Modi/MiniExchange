add_test([=[WorkloadGeneratorTest.SameSeedProducesIdenticalSequence]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.SameSeedProducesIdenticalSequence]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.SameSeedProducesIdenticalSequence]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:30]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.DifferentSeedProducesDifferentSequence]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.DifferentSeedProducesDifferentSequence]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.DifferentSeedProducesDifferentSequence]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:68]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.CancelOnlyReferencesLimitOrderIds]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.CancelOnlyReferencesLimitOrderIds]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.CancelOnlyReferencesLimitOrderIds]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:120]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.MixRatioMatchesConfig]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.MixRatioMatchesConfig]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.MixRatioMatchesConfig]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:159]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.PricesAlwaysPositive]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.PricesAlwaysPositive]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.PricesAlwaysPositive]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:220]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.OrderIdsMonotonicallyIncreasing]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.OrderIdsMonotonicallyIncreasing]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.OrderIdsMonotonicallyIncreasing]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:236]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.QuantitiesWithinConfiguredRange]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.QuantitiesWithinConfiguredRange]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.QuantitiesWithinConfiguredRange]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:267]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[WorkloadGeneratorTest.CancelNeverReferencesNonExistentId]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/workload_generator_test.exe [==[--gtest_filter=WorkloadGeneratorTest.CancelNeverReferencesNonExistentId]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorkloadGeneratorTest.CancelNeverReferencesNonExistentId]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/workload_generator_test.cpp:298]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(workload_generator_test_TESTS [==[WorkloadGeneratorTest.SameSeedProducesIdenticalSequence]==] [==[WorkloadGeneratorTest.DifferentSeedProducesDifferentSequence]==] [==[WorkloadGeneratorTest.CancelOnlyReferencesLimitOrderIds]==] [==[WorkloadGeneratorTest.MixRatioMatchesConfig]==] [==[WorkloadGeneratorTest.PricesAlwaysPositive]==] [==[WorkloadGeneratorTest.OrderIdsMonotonicallyIncreasing]==] [==[WorkloadGeneratorTest.QuantitiesWithinConfiguredRange]==] [==[WorkloadGeneratorTest.CancelNeverReferencesNonExistentId]==])
