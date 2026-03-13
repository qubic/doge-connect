# hash util library
add_library(hashutil
	hash_util/hash_util.h
	hash_util/hash_util.cpp
	hash_util/scrypt.h
	hash_util/scrypt.c
	hash_util/difficulty.h
	hash_util/difficulty.cpp
)
target_include_directories(hashutil 
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hash_util)

# dispatcher lib
set(SOURCES
	structs.h
	connection/connection.h
	connection/connection.cpp
	connection/qubic_connection.h
	connection/qubic_connection.cpp
	threads/input_thread.h
	threads/input_thread.cpp
	threads/stratum_recv_thread.h
	threads/stratum_recv_thread.cpp
	threads/qubic_recv_thread.h
	threads/qubic_recv_thread.cpp
	threads/task_dist_thread.h
	threads/task_dist_thread.cpp
	threads/share_valid_thread.h
	threads/share_valid_thread.cpp
	concurrency/concurrent_queue.h
	concurrency/concurrent_hashmap.h
)
add_library(dispatcherlib ${SOURCES})

target_include_directories(dispatcherlib 
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

target_link_libraries(dispatcherlib PRIVATE nlohmann_json::nlohmann_json hashutil)

# main executable
add_executable(qubicdogedispatcher dispatcher.cpp)

target_link_libraries(qubicdogedispatcher PRIVATE nlohmann_json::nlohmann_json hashutil dispatcherlib)