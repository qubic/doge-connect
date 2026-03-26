#include <string>
#include <iostream>

#include "log.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "config/config.h"
#include "crypto/dispatcher_signing.h"
#include "crypto/key_utils.h"
#include "connection/connection.h"
#include "connection/qubic_connection.h"
#include "concurrency/concurrent_queue.h"
#include "concurrency/concurrent_hashmap.h"
#include "threads/input_thread.h"
#include "threads/stratum_recv_thread.h"
#include "threads/task_dist_thread.h"
#include "threads/qubic_recv_thread.h"
#include "threads/share_valid_thread.h"
#include "hash_util/hash_util.h"
#include "hash_util/difficulty.h"
#include "structs.h"


/**
 * @brief The main Dispatcher application to act as a bridge between a doge mining pool and the Qubic network.
 * 
 * First opens connections to the mining pool via stratum TCP and to the Qubic network (Qubic TCP handshake).
 * Then starts the following threads for the main loop:
 * - inputThread: to react to key presses (currently supported: 'q' to quit).
 * - stratumRecvThread: to receive messages from the mining pool via stratum TCP.
 * - taskDistThread: to process received stratum messages and send them to the Qubic network.
 * - qubicRecvThread: to receive solutions from the qubic network.
 * - shareValidThread: to validate received solutions and submit to pool if difficulty is high enough.
 * 
 * @return 0 if the application completed without errors, 1 if the application terminated due to an error.
 */
int main(int argc, char* argv[])
{
    std::string configPath = "config.json";
    if (argc > 1)
        configPath = argv[1];

    auto configJson = loadConfigFile(configPath);
    if (!configJson)
        return 1;

    DispatcherAppConfig config;
    try
    {
        config = parseDispatcherConfig(*configJson);
    }
    catch (const std::exception& e)
    {
        ERR() << "Invalid config: " << e.what() << std::endl;
        return 1;
    }

    DispatcherSigningContext signingCtx;
    if (!initSigningContext(config.identity.seed, signingCtx))
    {
        ERR() << "Failed to derive signing keys from seed." << std::endl;
        return 1;
    }

    // Derive and print the dispatcher's public identity from the public key.
    char identity[61] = {0};
    getIdentityFromPublicKey(signingCtx.publicKey.data(), identity, false);

    // Print startup configuration summary.
    std::string maskedSeed = config.identity.seed.substr(0, 3) + std::string(49, '*') + config.identity.seed.substr(52, 3);
    LOG() << "=== Qubic Doge Dispatcher ===" << std::endl;
    LOG() << "Config:     " << configPath << std::endl;
    LOG() << "Pool:       " << config.pool.url << ":" << config.pool.stratumPort << std::endl;
    LOG() << "Worker:     " << config.pool.workerName << std::endl;
    LOG() << "Qubic IPs:  " << config.qubic.ips.size() << " (port " << config.qubic.port << ")" << std::endl;
    LOG() << "Seed:       " << maskedSeed << std::endl;
    LOG() << "Identity:   " << identity << std::endl;
    LOG() << "=============================" << std::endl;

    std::unique_ptr<ConnectionContext> context = ConnectionContext::makeConnectionContext();
    if (!context)
    {
        ERR() << "ConnectionContext (only relevant for WSAStartup on Windows) could not be created." << std::endl;
        return 1;
    }

    Connection stratumConnection;
    if (!stratumConnection.openConnection(config.pool.url, config.pool.stratumPort))
    {
        ERR() << "Stratum connection could not be opened." << std::endl;
        return 1;
    }
    else
        LOG() << "Stratum connection successfully opened." << std::endl;

    // Connect to all qubic peers in parallel so fast ones are ready immediately.
    // Max time per thread: 5s connect + 1s handshake + 0.2s = ~6.2s.
    std::vector<QubicConnection> qubicConnections(config.qubic.ips.size());
    {
        std::vector<std::thread> connectThreads;
        for (size_t i = 0; i < config.qubic.ips.size(); ++i)
        {
            qubicConnections[i].setPeer(config.qubic.ips[i], config.qubic.port);
            connectThreads.emplace_back([&conn = qubicConnections[i]]() {
                conn.reconnect();
            });
        }
        for (auto& t : connectThreads)
            t.join();

        unsigned int connected = 0;
        for (const auto& qc : qubicConnections)
            if (qc.isConnected()) connected++;
        LOG() << "Qubic peers: " << connected << "/" << config.qubic.ips.size() << " connected, rest will retry in background." << std::endl;
    }

    // Create a queue for received stratum messages.
    ConcurrentQueue<nlohmann::json> recvStratumMessages;
    ConcurrentQueue<DispatcherMiningSolution> recvQubicSolutions;
    ConcurrentHashMap<uint64_t, DispatcherMiningTask> activeTasks;

    std::vector<uint8_t> extraNonce1;

    if (!initStratumProtocol(stratumConnection, recvStratumMessages, extraNonce1,
            config.pool.workerName, config.pool.workerPassword))
        return 1;

    std::atomic<bool> keepRunning = true; // Atomic flag to signal the rest of the app to stop.
    std::atomic<uint64_t> nextStratumSendId = 3; // ids 1 and 2 were already used for protocol initialization

    // Maximum target for Dogecoin/Litecoin 0x1f00ffff (see https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client)
    DifficultyTarget dispatcherDifficulty(std::array<uint8_t, 4>({ 0xff, 0xff, 0x00, 0x1f })); // mantissa (LSB to MSB), exponent
    const DifficultyTarget poolBaseDifficulty(std::array<uint8_t, 4>({ 0xff, 0xff, 0x00, 0x1f })); // mantissa (LSB to MSB), exponent
    // poolCurrentDifficulty is only read/written in the taskDistThread. If we ever add more threads accessing this, it needs to have a mutex.
    DifficultyTarget poolCurrentDifficulty = poolBaseDifficulty;

    DispatcherStats stats;

    // Start all other threads. std::ref is needed here to force a pass by reference for the shared variables.
    // Start the input thread to react to key presses (currently supported: 'q' to quit).
    std::jthread inputThread(inputThreadLoop, std::ref(keepRunning));
    // Start the stratumRecvThread to receive messages from the mining pool via stratum TCP.
    std::jthread stratumRecvThread(stratumReceiveLoop, std::ref(recvStratumMessages), std::ref(stratumConnection),
        std::ref(config.pool), std::ref(extraNonce1));
    // Start taskDistThread to process received stratum messages and send them to the Qubic network.
    std::jthread taskDistThread(taskDistributionLoop, std::ref(recvStratumMessages), std::ref(activeTasks), std::ref(qubicConnections), std::ref(poolBaseDifficulty),
        std::ref(poolCurrentDifficulty), std::ref(dispatcherDifficulty), std::ref(extraNonce1), std::ref(signingCtx), std::ref(stats));
    // Start the qubicRecvThread to receive solutions from the qubic network.
    std::jthread qubicRecvThread(qubicReceiveLoop, std::ref(recvQubicSolutions), std::ref(qubicConnections));
    // Start the shareValidThread to validate received solutions and submit to pool if difficulty is high enough.
    std::jthread shareValidThread(shareValidationLoop, std::ref(recvQubicSolutions), std::ref(activeTasks),
        std::ref(nextStratumSendId), std::ref(stratumConnection), config.pool.workerName, std::ref(stats));

    LOG() << "Dispatcher running. Press 'q' to quit." << std::endl;

    while (keepRunning)
    {
        // TODO: replace the sleep below with condition_variable::wait_for(...) with the time and keepRunning.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        unsigned int connectedPeers = 0;
        for (const auto& qc : qubicConnections)
            if (qc.isConnected()) connectedPeers++;

        LOG() << "[status] net: " << connectedPeers << "/" << qubicConnections.size()
            << " | tasks: " << stats.tasksDistributed
            << " | sol recv/accepted/rejected: " << stats.solutionsReceived << "/" << stats.solutionsAccepted << "/" << stats.solutionsRejected
            << " | pool: " << stats.solutionsPassedPoolDiff
            << " | queues: stratum=" << recvStratumMessages.size() << " solutions=" << recvQubicSolutions.size()
            << " | active: " << activeTasks.size() << std::endl;

        // TODO: handle connection error (threads will shut down on recv/send error and need to be restarted after connection is re-established)
    }

    // Close connection so that recv does not block the stratumRecvThread/qubicRecvThread.
    stratumConnection.closeConnection();
    for (auto& qc : qubicConnections)
        qc.closeConnection();
    // Request stop of the taskDistThread manually and push a dummy object onto the queue to break the blocking pop().
    taskDistThread.request_stop();
    recvStratumMessages.push(nlohmann::json::object());
    // Request stop of the shareValidThread manually and push a dummy object onto the queue to break the blocking pop().
    shareValidThread.request_stop();
    recvQubicSolutions.push(DispatcherMiningSolution{});
    // The jthreads are joined automatically when they are destructed.

    return 0;
}
