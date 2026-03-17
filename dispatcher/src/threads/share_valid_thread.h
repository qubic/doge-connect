#pragma once

#include <stop_token>
#include <cstdint>

#include "concurrency/concurrent_queue.h"
#include "concurrency/concurrent_hashmap.h"
#include "connection/connection.h"
#include "structs.h"

/**
 * @brief Validate solutions and submit them to the mining pool if they pass the pool difficulty target.
 * @param st Stop token of the thread.
 * @param queue Queue of received mining solutions.
 * @param activeTasks Currently active tasks for which solutions may be accepted.
 * @param nextStratumSendId Monotonically increasing counter for the mining.submit messages to be sent via stratum.
 * @param connection Stratum TCP connection to the mining pool.
 * @param workerName The name under which the Dispatcher is known to the mining pool.
 */
void shareValidationLoop(
    std::stop_token st,
    ConcurrentQueue<DispatcherMiningSolution>& queue,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::atomic<uint64_t>& nextStratumSendId,
    Connection& connection,
    const std::string& workerName,
    DispatcherStats& stats
);