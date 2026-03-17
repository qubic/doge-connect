#pragma once

#include <stop_token>
#include <cstdint>
#include <atomic>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "concurrency/concurrent_hashmap.h"
#include "connection/qubic_connection.h"
#include "crypto/dispatcher_signing.h"
#include "hash_util/difficulty.h"
#include "structs.h"

/**
 * @brief Distribute received stratum mining tasks to the Qubic network.
 * @param st Stop token of the thread.
 * @param queue A FIFO queue of received mining tasks.
 * @param activeTasks Currently active, i.e. already distributed, mining tasks.
 * @param connections The connections to send the tasks to.
 * @param basePoolDifficulty The pool's constant base difficulty.
 * @param currentPoolDifficulty The current pool difficulty.
 * @param dispatcherDifficulty The current Dispatcher difficulty.
 * @param extraNonce1 The extraNonce1 received from the pool via stratum in the mining.subscribe response.
 * @param extraNonce2NumBytes The number of bytes for the extraNonce2, may be updated on stratum reconnect.
 */
void taskDistributionLoop(
    std::stop_token st,
    ConcurrentQueue<nlohmann::json>& queue,
    ConcurrentHashMap<uint64_t, DispatcherMiningTask>& activeTasks,
    std::vector<QubicConnection>& connections,
    const DifficultyTarget& basePoolDifficulty,
    DifficultyTarget& currentPoolDifficulty,
    const DifficultyTarget& dispatcherDifficulty,
    const std::vector<uint8_t>& extraNonce1,
    std::atomic<unsigned int>& extraNonce2NumBytes,
    const DispatcherSigningContext& signingCtx,
    DispatcherStats& stats
);