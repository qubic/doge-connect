#pragma once

#include <stop_token>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"

#include "mining_structs.h"

/**
* @brief Receive tasks from the Dispatcher.
* @param st Stop token of the thread.
* @param activeTasks Queue of received and currently active tasks.
* @param connection Connection to the Dispatcher.
* @param startNewJob An atomic flag to signal the mining thread to immediately start a new task.
*/
void taskReceiveLoop(std::stop_token st, ConcurrentQueue<InternalMiningTask>& activeTasks, QubicConnection& connection, std::atomic<bool>& startNewJob);