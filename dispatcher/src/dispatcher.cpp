#include <string>
#include <iostream>
#include <fstream>

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

    // Scrypt stratum difficulty base: 0x1f00ffff (scrypt diff 1).
    // All scrypt miners and pools use this base. The earlier "high-hash" rejections were caused
    // by wrong prevHash byte order (full reversal vs word-swap), not wrong difficulty base.
    const DifficultyTarget poolBaseDifficulty(std::array<uint8_t, 4>({ 0xff, 0xff, 0x00, 0x1f })); // mantissa (LSB to MSB), exponent
    // poolCurrentDifficulty is only read/written in the taskDistThread. If we ever add more threads accessing this, it needs to have a mutex.
    DifficultyTarget poolCurrentDifficulty = poolBaseDifficulty;

    DispatcherStats stats;

    if (config.dryRun)
        LOG() << "*** DRY RUN MODE: connected to stratum + network but NOT distributing tasks or processing solutions ***" << std::endl;

    // Start all other threads. std::ref is needed here to force a pass by reference for the shared variables.
    // Start the input thread to react to key presses (currently supported: 'q' to quit).
    std::jthread inputThread(inputThreadLoop, std::ref(keepRunning));
    // Start the stratumRecvThread to receive messages from the mining pool via stratum TCP.
    std::jthread stratumRecvThread(stratumReceiveLoop, std::ref(recvStratumMessages), std::ref(stratumConnection),
        std::ref(config.pool), std::ref(extraNonce1));

    // In dry mode: skip task distribution and solution processing threads.
    // Instead, drain the stratum queue to track difficulty and count tasks.
    std::optional<std::jthread> taskDistThread, qubicRecvThread, shareValidThread, dryDrainThread;
    if (!config.dryRun)
    {
        taskDistThread.emplace(taskDistributionLoop, std::ref(recvStratumMessages), std::ref(activeTasks), std::ref(qubicConnections), std::ref(poolBaseDifficulty),
            std::ref(poolCurrentDifficulty), std::ref(extraNonce1), std::ref(signingCtx), std::ref(stats));
        qubicRecvThread.emplace(qubicReceiveLoop, std::ref(recvQubicSolutions), std::ref(qubicConnections));
        shareValidThread.emplace(shareValidationLoop, std::ref(recvQubicSolutions), std::ref(activeTasks),
            std::ref(nextStratumSendId), std::ref(stratumConnection), config.pool.workerName, std::ref(stats));
    }
    else
    {
        // Dry mode: drain stratum messages to track difficulty without distributing tasks.
        dryDrainThread.emplace([&](std::stop_token st) {
            while (!st.stop_requested())
            {
                nlohmann::json msg = recvStratumMessages.pop();
                if (!msg.contains("id")) continue;
                if (msg["id"] == nullptr && msg.contains("method"))
                {
                    if (msg["method"] == "mining.set_difficulty")
                    {
                        uint64_t diff = msg["params"][0];
                        stats.poolDifficulty.store(diff);
                        LOG() << "Dry mode: pool difficulty updated to " << diff << std::endl;
                    }
                    else if (msg["method"] == "mining.notify")
                    {
                        stats.tasksDistributed++;
                    }
                }
            }
        });
    }

    LOG() << "Dispatcher running. Press 'q' to quit." << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    // Hashrate estimation using a rolling window of accepted solutions.
    auto hashrateWindowStart = std::chrono::steady_clock::now();
    uint64_t windowStartAccepted = 0;
    double estimatedHashrate = 0.0;
    constexpr double hashrateWindowSec = 300.0; // 5-minute rolling window

    while (keepRunning)
    {
        // TODO: replace the sleep below with condition_variable::wait_for(...) with the time and keepRunning.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        unsigned int connectedPeers = 0;
        for (const auto& qc : qubicConnections)
            if (qc.isConnected()) connectedPeers++;

        // Calculate hashrate over a rolling window.
        // For scrypt with 0x1f base: hashes_per_share = diff * 65536.
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - hashrateWindowStart).count();
        uint64_t currentAccepted = stats.solutionsAccepted.load();
        double currentDiff = static_cast<double>(stats.poolDifficulty.load());
        uint64_t windowShares = currentAccepted - windowStartAccepted;

        if (elapsedSec > 0 && windowShares > 0)
            estimatedHashrate = static_cast<double>(windowShares) * currentDiff * 65536.0 / elapsedSec;

        // Reset window periodically to avoid stale data dominating.
        if (elapsedSec >= hashrateWindowSec)
        {
            hashrateWindowStart = now;
            windowStartAccepted = currentAccepted;
        }

        // Format hashrate with appropriate unit.
        std::string hrStr;
        if (estimatedHashrate >= 1e12)
            hrStr = std::to_string(static_cast<int>(estimatedHashrate / 1e12)) + " TH/s";
        else if (estimatedHashrate >= 1e9)
            hrStr = std::to_string(static_cast<int>(estimatedHashrate / 1e9)) + " GH/s";
        else if (estimatedHashrate >= 1e6)
            hrStr = std::to_string(static_cast<int>(estimatedHashrate / 1e6)) + " MH/s";
        else if (estimatedHashrate >= 1e3)
            hrStr = std::to_string(static_cast<int>(estimatedHashrate / 1e3)) + " KH/s";
        else
            hrStr = std::to_string(static_cast<int>(estimatedHashrate)) + " H/s";

        LOG() << "[status] net: " << connectedPeers << "/" << qubicConnections.size()
            << " | diff: " << stats.poolDifficulty.load()
            << " | hr: " << hrStr
            << " | tasks: " << stats.tasksDistributed
            << " | sol recv/acc/rej/stale: " << stats.solutionsReceived << "/" << stats.solutionsAccepted << "/" << stats.solutionsRejected << "/" << stats.solutionsStale
            << " | pool: " << stats.solutionsPassedPoolDiff << " (acc/rej: " << stats.poolSharesAccepted << "/" << stats.poolSharesRejected << ")"
            << " | queues: stratum=" << recvStratumMessages.size() << " solutions=" << recvQubicSolutions.size()
            << " | active: " << activeTasks.size() << std::endl;

        // Write JSON stats file if configured.
        if (!config.statsFile.empty())
        {
            uint64_t uptimeSec = static_cast<uint64_t>(
                std::chrono::duration<double>(now - startTime).count());

            nlohmann::json statsJson = {
                {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()},
                {"uptime_seconds", uptimeSec},
                {"network", {
                    {"connected_peers", connectedPeers},
                    {"total_peers", qubicConnections.size()}
                }},
                {"mining", {
                    {"hashrate", static_cast<uint64_t>(estimatedHashrate)},
                    {"hashrate_display", hrStr},
                    {"pool_difficulty", stats.poolDifficulty.load()},
                    {"tasks_distributed", stats.tasksDistributed.load()}
                }},
                {"solutions", {
                    {"received", stats.solutionsReceived.load()},
                    {"accepted", stats.solutionsAccepted.load()},
                    {"rejected", stats.solutionsRejected.load()},
                    {"stale", stats.solutionsStale.load()}
                }},
                {"pool", {
                    {"submitted", stats.solutionsPassedPoolDiff.load()},
                    {"accepted", stats.poolSharesAccepted.load()},
                    {"rejected", stats.poolSharesRejected.load()}
                }},
                {"queues", {
                    {"stratum", recvStratumMessages.size()},
                    {"solutions", recvQubicSolutions.size()}
                }},
                {"active_tasks", activeTasks.size()}
            };

            // Write atomically: write to tmp file then rename.
            std::string tmpPath = config.statsFile + ".tmp";
            std::ofstream out(tmpPath);
            if (out.is_open())
            {
                out << statsJson.dump(2) << std::endl;
                out.close();
                std::rename(tmpPath.c_str(), config.statsFile.c_str());
            }
        }
    }

    // Close connection so that recv does not block the stratumRecvThread/qubicRecvThread.
    stratumConnection.closeConnection();
    for (auto& qc : qubicConnections)
        qc.closeConnection();
    // Request stop of the taskDistThread manually and push a dummy object onto the queue to break the blocking pop().
    if (taskDistThread) { taskDistThread->request_stop(); recvStratumMessages.push(nlohmann::json::object()); }
    if (dryDrainThread) { dryDrainThread->request_stop(); recvStratumMessages.push(nlohmann::json::object()); }
    if (shareValidThread) { shareValidThread->request_stop(); recvQubicSolutions.push(DispatcherMiningSolution{}); }
    // The jthreads are joined automatically when they are destructed.

    return 0;
}
