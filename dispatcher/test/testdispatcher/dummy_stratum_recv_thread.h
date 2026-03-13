#pragma once

#include <stop_token>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"

/**
 * @brief Mimic a loop continuously receiving stratum messages from a mining pool.
 * @param st Stop token of the thread.
 * @param queue Queue to push the "received" stratum messages onto for further processing.
 * @param timeBetweenJobsSec Time in seconds between pushing jobs onto the queue.
 * @param frequencyClearJobs For n > 0, every n-th job will have the clearJobQueue flag set to true.
 *                           For n == 0, the clearJobQueue flag will be set to false for all jobs.
 */
void dummyStratumReceiveLoop(std::stop_token st, ConcurrentQueue<nlohmann::json>& queue, unsigned int timeBetweenJobsSec, unsigned int frequencyClearJobs);
