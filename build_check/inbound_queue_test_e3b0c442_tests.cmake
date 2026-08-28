add_test([=[FramingTest.EmptyPayload]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=FramingTest.EmptyPayload]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[FramingTest.EmptyPayload]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:36]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[FramingTest.SmallPayload]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=FramingTest.SmallPayload]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[FramingTest.SmallPayload]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:46]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[FramingTest.LengthPrefixIsBigEndian]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=FramingTest.LengthPrefixIsBigEndian]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[FramingTest.LengthPrefixIsBigEndian]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:67]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[FramingTest.PayloadIntegrity]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=FramingTest.PayloadIntegrity]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[FramingTest.PayloadIntegrity]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:79]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[InboundQueueTest.PushPopLimitOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=InboundQueueTest.PushPopLimitOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[InboundQueueTest.PushPopLimitOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:96]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[InboundQueueTest.PushPopMarketOrder]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=InboundQueueTest.PushPopMarketOrder]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[InboundQueueTest.PushPopMarketOrder]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:119]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[InboundQueueTest.PushPopCancelRequest]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=InboundQueueTest.PushPopCancelRequest]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[InboundQueueTest.PushPopCancelRequest]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:141]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OutboundQueueTest.PushPopResponse]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=OutboundQueueTest.PushPopResponse]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OutboundQueueTest.PushPopResponse]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:163]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ParseToPushTest.ValidLimitOrderParsesAndQueues]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=ParseToPushTest.ValidLimitOrderParsesAndQueues]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ParseToPushTest.ValidLimitOrderParsesAndQueues]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:192]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ParseToPushTest.ValidCancelParsesAndQueues]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=ParseToPushTest.ValidCancelParsesAndQueues]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ParseToPushTest.ValidCancelParsesAndQueues]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:215]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ParseToPushTest.ParseErrorRendersDirectResponse]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=ParseToPushTest.ParseErrorRendersDirectResponse]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ParseToPushTest.ParseErrorRendersDirectResponse]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:237]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[ParseToPushTest.QueueFullRejectsSilently]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/inbound_queue_test.exe [==[--gtest_filter=ParseToPushTest.QueueFullRejectsSilently]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[ParseToPushTest.QueueFullRejectsSilently]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/inbound_queue_test.cpp:256]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(inbound_queue_test_TESTS [==[FramingTest.EmptyPayload]==] [==[FramingTest.SmallPayload]==] [==[FramingTest.LengthPrefixIsBigEndian]==] [==[FramingTest.PayloadIntegrity]==] [==[InboundQueueTest.PushPopLimitOrder]==] [==[InboundQueueTest.PushPopMarketOrder]==] [==[InboundQueueTest.PushPopCancelRequest]==] [==[OutboundQueueTest.PushPopResponse]==] [==[ParseToPushTest.ValidLimitOrderParsesAndQueues]==] [==[ParseToPushTest.ValidCancelParsesAndQueues]==] [==[ParseToPushTest.ParseErrorRendersDirectResponse]==] [==[ParseToPushTest.QueueFullRejectsSilently]==])
