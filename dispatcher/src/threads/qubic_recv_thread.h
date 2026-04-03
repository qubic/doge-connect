#pragma once

#include <stop_token>

#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "structs.h"

/**
 * @brief Receive mining solutions sent via the Qubic network.
 *
 * Polls all qubic peer connections for incoming packets, reassembles TCP streams
 * into complete qubic packets, validates solution signatures, and pushes valid
 * solutions onto the queue for share validation. Handles automatic reconnection
 * with exponential backoff for disconnected peers.
 *
 * @param st Stop token of the thread.
 * @param queue The queue of received solutions (waiting for validation).
 * @param connections The connections to the Qubic network.
 * @param peerStats Per-peer statistics (reconnects, solutions received, bytes, etc.).
 */
void qubicReceiveLoop(std::stop_token st, ConcurrentQueue<DispatcherMiningSolution>& queue, std::vector<QubicConnection>& connections, std::vector<PeerStats>& peerStats);