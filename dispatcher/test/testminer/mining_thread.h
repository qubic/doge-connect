#pragma once

#include <stop_token>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "crypto/dispatcher_signing.h"

#include "mining_structs.h"

/**
* @brief Perform mining on the active tasks and send found solutions to the Dispatcher.
* @param st Stop token of the thread.
* @param activeTasks Currently active tasks.
* @param connection Connection to the Dispatcher.
* @param startNewJob An atomic flag that signals when to start with a new job immediately.
* @param solutionsFound Total number of solutions found.
* @param signingCtx The miner's signing context for signing solutions.
*/
void miningLoop(
    std::stop_token st,
    ConcurrentQueue<InternalMiningTask>& activeTasks,
    QubicConnection& connection,
    std::atomic<bool>& startNewJob,
    std::atomic<uint64_t>& solutionsFound,
    const DispatcherSigningContext& signingCtx
);
