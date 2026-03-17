#include <string>
#include <iostream>
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
 * @brief Initialize the stratum connection via mining.subscribe and mining.authorize messages.
 * @param connection The stratum TCP connection to the mining pool.
 * @param recvStratumMessages Queue of received stratum messages. The pool might send the first task immediately with the response
                              to the initialization. The queue is used to forward it to the task distribution thread.
 * @param extraNonce1 Output parameter for the extraNonce1 received from the pool.
 * @param extraNonce2NumBytes Output parameter for the size of the extraNonce2 received from the pool.
 * @return True if the initialization was successful, false otherwise.
 */
bool initStratumProtocol(
    Connection& connection,
    ConcurrentQueue<nlohmann::json>& recvStratumMessages,
    std::vector<uint8_t>& extraNonce1,
    unsigned int& extraNonce2NumBytes,
    const std::string& workerName,
    const std::string& workerPassword
)
{
    // Send mining.subscribe
    connection.sendMessage("{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n"); // Stratum messages must end in newline
    std::string subscribeResponse = connection.receiveResponse();
    if (subscribeResponse.empty())
    {
        std::cerr << "No response received from pool for mining.subscribe." << std::endl;
        return false;
    }
    auto response = nlohmann::json::parse(subscribeResponse);
    if (response["id"] == 1 && response["error"] == nullptr)
    {
        const nlohmann::json& result = response["result"];
        // TODO: save subscription IDs sent in index 0 to reconnect if the TCP connection is dropped.
        std::string extraNonce1String = result[1];
        extraNonce1 = hexToBytes(extraNonce1String, ByteArrayFormat::BigEndian);
        extraNonce2NumBytes = result[2];
        std::cout << "Received extraNonce1 " << extraNonce1String << ", size of extraNonce2 in bytes: " << extraNonce2NumBytes << std::endl;
    }
    else
    {
        std::cerr << "Mining subscribe response could not be parsed." << std::endl;
        return false;
    }

    // Send mining.authorize
    std::string auth = "{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\""
        + workerName + "\", \"" + workerPassword + "\"]}\n"; // Stratum messages must end in newline
    connection.sendMessage(auth);
    // The response typically consists of 3 JSON objects:
    // - the actual response with id 2
    // - a mining.set_difficulty message
    // - the first mining task (mining.notify)
    std::string responseString = connection.receiveResponse();
    std::size_t pos = 0, end;
    while ((end = responseString.find('\n', pos)) != std::string::npos || pos < responseString.size())
    {
        if (end == std::string::npos)
            end = responseString.size();
        std::string line = responseString.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty())
            continue;
        response = nlohmann::json::parse(line);
        if (response["id"] == 2)
        {
            // Check that authorization worked.
            if (response["result"] == false || response["error"] != nullptr)
            {
                std::cerr << "Mining authorization did not work." << std::endl;
                return false;
            }
        }
        else if (response["id"] == nullptr)
        {
            recvStratumMessages.push(response);
        }
    }

    return true;
}

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
        std::cerr << "Invalid config: " << e.what() << std::endl;
        return 1;
    }

    DispatcherSigningContext signingCtx;
    if (!initSigningContext(config.identity.seed, signingCtx))
    {
        std::cerr << "Failed to derive signing keys from seed." << std::endl;
        return 1;
    }

    // Derive and print the dispatcher's public identity from the public key.
    char identity[61] = {0};
    getIdentityFromPublicKey(signingCtx.publicKey.data(), identity, false);

    // Print startup configuration summary.
    std::string maskedSeed = config.identity.seed.substr(0, 3) + std::string(49, '*') + config.identity.seed.substr(52, 3);
    std::cout << "=== Qubic Doge Dispatcher ===" << std::endl;
    std::cout << "Config:     " << configPath << std::endl;
    std::cout << "Pool:       " << config.pool.url << ":" << config.pool.stratumPort << std::endl;
    std::cout << "Worker:     " << config.pool.workerName << std::endl;
    std::cout << "Qubic IPs:  " << config.qubic.ips.size() << " (port " << config.qubic.port << ")" << std::endl;
    std::cout << "Seed:       " << maskedSeed << std::endl;
    std::cout << "Identity:   " << identity << std::endl;
    std::cout << "=============================" << std::endl;

    std::unique_ptr<ConnectionContext> context = ConnectionContext::makeConnectionContext();
    if (!context)
    {
        std::cerr << "ConnectionContext (only relevant for WSAStartup on Windows) could not be created." << std::endl;
        return 1;
    }

    Connection stratumConnection;
    if (!stratumConnection.openConnection(config.pool.url, config.pool.stratumPort))
    {
        std::cerr << "Stratum connection could not be opened." << std::endl;
        return 1;
    }
    else
        std::cout << "Stratum connection successfully opened." << std::endl;

    std::vector<QubicConnection> qubicConnections;
    for (int i = 0; i < config.qubic.ips.size(); ++i)
    {
        QubicConnection qc;
        if (!qc.openQubicConnection(config.qubic.ips[i], config.qubic.port))
            std::cerr << "Qubic connection could not be opened to IP " << config.qubic.ips[i] << std::endl;
        else
            qubicConnections.push_back(std::move(qc));
    }
    if (qubicConnections.empty())
    {
        std::cerr << "No Qubic connection was opened successfully (out of " << config.qubic.ips.size() << " provided IPs)." << std::endl;
        return 1;
    }
    else
        std::cout << "Qubic connections opened successfully: " << qubicConnections.size() << "/" << config.qubic.ips.size() << " IPs." << std::endl;

    // Create a queue for received stratum messages.
    ConcurrentQueue<nlohmann::json> recvStratumMessages;
    ConcurrentQueue<DispatcherMiningSolution> recvQubicSolutions;
    ConcurrentHashMap<uint64_t, DispatcherMiningTask> activeTasks;

    std::vector<uint8_t> extraNonce1;
    unsigned int extraNonce2NumBytes;

    if (!initStratumProtocol(stratumConnection, recvStratumMessages, extraNonce1, extraNonce2NumBytes,
            config.pool.workerName, config.pool.workerPassword))
        return 1;

    std::atomic<bool> keepRunning = true; // Atomic flag to signal the rest of the app to stop.
    std::atomic<uint64_t> numericDispatcherJobId = 0;
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
    std::jthread stratumRecvThread(stratumReceiveLoop, std::ref(recvStratumMessages), std::ref(stratumConnection));
    // Start taskDistThread to process received stratum messages and send them to the Qubic network.
    std::jthread taskDistThread(taskDistributionLoop, std::ref(recvStratumMessages), std::ref(activeTasks), std::ref(qubicConnections), std::ref(poolBaseDifficulty),
        std::ref(poolCurrentDifficulty), std::ref(dispatcherDifficulty), std::ref(numericDispatcherJobId), std::ref(extraNonce1), extraNonce2NumBytes, std::ref(signingCtx), std::ref(stats));
    // Start the qubicRecvThread to receive solutions from the qubic network.
    std::jthread qubicRecvThread(qubicReceiveLoop, std::ref(recvQubicSolutions), std::ref(qubicConnections));
    // Start the shareValidThread to validate received solutions and submit to pool if difficulty is high enough.
    std::jthread shareValidThread(shareValidationLoop, std::ref(recvQubicSolutions), std::ref(activeTasks),
        std::ref(nextStratumSendId), std::ref(stratumConnection), config.pool.workerName, std::ref(stats));

    std::cout << "Dispatcher running. Press 'q' to quit." << std::endl;

    while (keepRunning)
    {
        // TODO: replace the sleep below with condition_variable::wait_for(...) with the time and keepRunning.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        std::cout << "[status] tasks distributed: " << stats.tasksDistributed
            << " | solutions recv/accepted/rejected: " << stats.solutionsReceived << "/" << stats.solutionsAccepted << "/" << stats.solutionsRejected
            << " | pool shares: " << stats.solutionsPassedPoolDiff
            << " | queues: stratum=" << recvStratumMessages.size() << " solutions=" << recvQubicSolutions.size()
            << " | active tasks: " << activeTasks.size() << std::endl;

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
