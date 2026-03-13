#pragma once

#include <stop_token>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "structs.h"

/** 
* @brief Receive mining solutions sent via the Qubic network.
* @param st Stop token of the thread.
* @param queue The queue of received solutions (waiting for validation).
* @param connections The connections to the Qubic network.
*/
void qubicReceiveLoop(std::stop_token st, ConcurrentQueue<DispatcherMiningSolution>& queue, std::vector<QubicConnection>& connections);