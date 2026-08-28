add_test([=[UdpE2EDoDTest.ReconstructedBookMatchesEngine]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_e2e_dod_test.exe [==[--gtest_filter=UdpE2EDoDTest.ReconstructedBookMatchesEngine]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[UdpE2EDoDTest.ReconstructedBookMatchesEngine]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_e2e_dod_test.cpp:109]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[UdpE2EDoDTest.ReconstructionAfterDrainAndRecover]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_e2e_dod_test.exe [==[--gtest_filter=UdpE2EDoDTest.ReconstructionAfterDrainAndRecover]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[UdpE2EDoDTest.ReconstructionAfterDrainAndRecover]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_e2e_dod_test.cpp:163]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[UdpE2EDoDTest.GapDetectionUnderPacketLoss]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_e2e_dod_test.exe [==[--gtest_filter=UdpE2EDoDTest.GapDetectionUnderPacketLoss]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[UdpE2EDoDTest.GapDetectionUnderPacketLoss]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_e2e_dod_test.cpp:210]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[UdpE2EDoDTest.NoDropsNoStaleness]=]  C:/Users/harsh/Desktop/MiniExchange/build_check/udp_e2e_dod_test.exe [==[--gtest_filter=UdpE2EDoDTest.NoDropsNoStaleness]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[UdpE2EDoDTest.NoDropsNoStaleness]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[C:/Users/harsh/Desktop/MiniExchange/tests/udp_e2e_dod_test.cpp:292]==]
    WORKING_DIRECTORY [==[C:/Users/harsh/Desktop/MiniExchange/build_check]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(udp_e2e_dod_test_TESTS [==[UdpE2EDoDTest.ReconstructedBookMatchesEngine]==] [==[UdpE2EDoDTest.ReconstructionAfterDrainAndRecover]==] [==[UdpE2EDoDTest.GapDetectionUnderPacketLoss]==] [==[UdpE2EDoDTest.NoDropsNoStaleness]==])
