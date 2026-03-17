#pragma once

#include <stop_token>
#include <atomic>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "connection/connection.h"
#include "config/config.h"

/**
 * @brief Initialize the stratum connection via mining.subscribe and mining.authorize messages.
 * @return True if the initialization was successful, false otherwise.
 */
bool initStratumProtocol(
    Connection& connection,
    ConcurrentQueue<nlohmann::json>& recvStratumMessages,
    std::vector<uint8_t>& extraNonce1,
    std::atomic<unsigned int>& extraNonce2NumBytes,
    const std::string& workerName,
    const std::string& workerPassword
);

/**
 * @brief Receive mining tasks from a pool via stratum. Reconnects automatically on disconnect.
 * @param st Stop token of the thread.
 * @param queue The queue of received stratum mining tasks.
 * @param connection The stratum TCP connection to the mining pool.
 * @param poolConfig Pool connection configuration for reconnecting.
 * @param extraNonce1 Shared extraNonce1, updated on reconnect.
 * @param extraNonce2NumBytes Shared extraNonce2 size, updated on reconnect.
 */
void stratumReceiveLoop(
    std::stop_token st,
    ConcurrentQueue<nlohmann::json>& queue,
    Connection& connection,
    const PoolConfig& poolConfig,
    std::vector<uint8_t>& extraNonce1,
    std::atomic<unsigned int>& extraNonce2NumBytes
);
