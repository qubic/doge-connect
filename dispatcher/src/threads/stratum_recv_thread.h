#pragma once

#include <stop_token>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "connection/connection.h"

/**
 * @brief Receive mining tasks from a pool via stratum.
 * @param st Stop token of the thread.
 * @param queue The queue of received stratum mining tasks.
 * @param connection The stratum TCP connection to the mining pool.
 */
void stratumReceiveLoop(std::stop_token st, ConcurrentQueue<nlohmann::json>& queue, Connection& connection);
