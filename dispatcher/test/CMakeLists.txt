# test dispatcher executable
add_executable(testdispatcher
	testdispatcher/test_dispatcher.cpp
	testdispatcher/dummy_stratum_recv_thread.h
	testdispatcher/dummy_stratum_recv_thread.cpp
)

target_link_libraries(testdispatcher PRIVATE nlohmann_json::nlohmann_json hashutil dispatcherlib)

# test miner executable
add_executable(testminer
	testminer/test_miner.cpp
	testminer/task_recv_thread.h
	testminer/task_recv_thread.cpp
	testminer/mining_thread.h
	testminer/mining_thread.cpp
	testminer/mining_structs.h
)

target_link_libraries(testminer PRIVATE hashutil dispatcherlib)

# test signing executable
add_executable(testsigning
	testsigning/test_signing.cpp
)
target_link_libraries(testsigning PRIVATE qubiccrypto)