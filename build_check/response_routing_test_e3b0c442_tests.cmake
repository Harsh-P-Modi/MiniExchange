add_test([=[ResponseRoutingTest.MultipleClientsGetOwnResponses]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/response_routing_test.exe [==[--gtest_filter=ResponseRoutingTest.MultipleClientsGetOwnResponses]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ResponseRoutingTest.MultipleClientsGetOwnResponses]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/response_routing_test.cpp:46]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ResponseRoutingTest.CrossTalkVerification]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/response_routing_test.exe [==[--gtest_filter=ResponseRoutingTest.CrossTalkVerification]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ResponseRoutingTest.CrossTalkVerification]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/response_routing_test.cpp:112]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ResponseRoutingTest.MixedCommandTypesRoutedCorrectly]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/response_routing_test.exe [==[--gtest_filter=ResponseRoutingTest.MixedCommandTypesRoutedCorrectly]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ResponseRoutingTest.MixedCommandTypesRoutedCorrectly]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/response_routing_test.cpp:159]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ResponseRoutingTest.DisconnectedClientResponseDropped]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/response_routing_test.exe [==[--gtest_filter=ResponseRoutingTest.DisconnectedClientResponseDropped]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ResponseRoutingTest.DisconnectedClientResponseDropped]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/response_routing_test.cpp:227]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ResponseRoutingTest.OwnerThreadsFromTaggedCommandToRestingOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/response_routing_test.exe [==[--gtest_filter=ResponseRoutingTest.OwnerThreadsFromTaggedCommandToRestingOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ResponseRoutingTest.OwnerThreadsFromTaggedCommandToRestingOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/response_routing_test.cpp:283]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(response_routing_test_TESTS [==[ResponseRoutingTest.MultipleClientsGetOwnResponses]==] [==[ResponseRoutingTest.CrossTalkVerification]==] [==[ResponseRoutingTest.MixedCommandTypesRoutedCorrectly]==] [==[ResponseRoutingTest.DisconnectedClientResponseDropped]==] [==[ResponseRoutingTest.OwnerThreadsFromTaggedCommandToRestingOrder]==])
