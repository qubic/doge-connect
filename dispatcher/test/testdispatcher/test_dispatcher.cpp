#include <string>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "config/config.h"
#include "connection/connection.h"
#include "connection/qubic_connection.h"
#include "concurrency/concurrent_queue.h"
#include "concurrency/concurrent_hashmap.h"
#include "dummy_stratum_recv_thread.h"
#include "threads/input_thread.h"
#include "threads/task_dist_thread.h"
#include "threads/qubic_recv_thread.h"
#include "threads/share_valid_thread.h"
#include "hash_util/hash_util.h"
#include "hash_util/difficulty.h"
#include "structs.h"

/**
 * @brief The test Dispatcher application that mimics a bridge between a doge mining pool and the Qubic network.
 *
 * In contrast to the real qubicdogedispatcher application, the testdispatcher does not have a real stratum connection to a mining pool.
 * Instead, pre-recorded example tasks are distributed to the Qubic network for testing.
 * 
 * First opens connections to the Qubic network (Qubic TCP handshake).
 * Then starts the following threads for the main loop:
 * - inputThread: to react to key presses (currently supported: 'q' to quit).
 * - DUMMY stratumRecvThread: to continuously push stratum messages onto the queue.
 * - taskDistThread: to process received stratum messages and send them to the Qubic network.
 * - qubicRecvThread: to receive solutions from the qubic network.
 * - shareValidThread: to validate received solutions, the testdispatcher does not submit to a pool.
 *
 * @return 0 if the application completed without errors, 1 if the application terminated due to an error.
 */
int main(int argc, char* argv[])
{
    std::string configPath = "test_dispatcher_config.json";
    if (argc > 1)
        configPath = argv[1];

    auto configJson = loadConfigFile(configPath);
    if (!configJson)
        return 1;

    TestDispatcherAppConfig config;
    try
    {
        config = parseTestDispatcherConfig(*configJson);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid config: " << e.what() << std::endl;
        return 1;
    }

    std::unique_ptr<ConnectionContext> context = ConnectionContext::makeConnectionContext();
    if (!context)
    {
        std::cerr << "ConnectionContext (only relevant for WSAStartup on Windows) could not be created." << std::endl;
        return 1;
    }

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

    std::vector<uint8_t> extraNonce1 = hexToBytes("6cde77a1", ByteArrayFormat::BigEndian);
    unsigned int extraNonce2NumBytes = 8;

    std::atomic<bool> keepRunning = true; // Atomic flag to signal the rest of the app to stop.
    std::atomic<uint64_t> numericDispatcherJobId = 0;

    // Maximum target for Dogecoin/Litecoin 0x1f00ffff (see https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client)
    DifficultyTarget dispatcherDifficulty(std::array<uint8_t, 4>({ 0xff, 0xff, 0x00, 0x1f })); // mantissa (LSB to MSB), exponent
    const DifficultyTarget poolBaseDifficulty(std::array<uint8_t, 4>({ 0xff, 0xff, 0x00, 0x1f })); // mantissa (LSB to MSB), exponent
    // poolCurrentDifficulty is only read/written in the taskDistThread. If we ever add more threads accessing this, it needs to have a mutex.
    DifficultyTarget poolCurrentDifficulty = poolBaseDifficulty;

    // Add mining.set_difficulty message to the queue.
    recvStratumMessages.push(nlohmann::json(R"({"id": null, "method": "mining.set_difficulty", "params": [65536]})"));

    // Start all other threads. std::ref is needed here to force a pass by reference for the shared variables.
    // Start the input thread to react to key presses (currently supported: 'q' to quit).
    std::jthread inputThread(inputThreadLoop, std::ref(keepRunning));
    // Start the dummy stratumRecvThread to continuously push stratum messages onto the queue.
    std::jthread stratumRecvThread(dummyStratumReceiveLoop, std::ref(recvStratumMessages), config.testDispatcher.timeBetweenJobsSec, config.testDispatcher.frequencyClearJobs);
    // Start taskDistThread to process received stratum messages and send them to the Qubic network.
    std::jthread taskDistThread(taskDistributionLoop, std::ref(recvStratumMessages), std::ref(activeTasks), std::ref(qubicConnections), std::ref(poolBaseDifficulty),
        std::ref(poolCurrentDifficulty), std::ref(dispatcherDifficulty), std::ref(numericDispatcherJobId), std::ref(extraNonce1), extraNonce2NumBytes);
    // Start the qubicRecvThread to receive solutions from the qubic network.
    std::jthread qubicRecvThread(qubicReceiveLoop, std::ref(recvQubicSolutions), std::ref(qubicConnections));
    // Start the shareValidThread to validate received solutions. The test dispatcher does not submit to a pool, so fill data with dummies.
    std::atomic<uint64_t> nextStratumSendId = 3; // ids 1 and 2 were already used for protocol initialization
    Connection dummyStratumConnection;
    std::jthread shareValidThread(shareValidationLoop, std::ref(recvQubicSolutions), std::ref(activeTasks),
        std::ref(nextStratumSendId), std::ref(dummyStratumConnection), /*workerName=*/"");

    while (keepRunning)
    {
        std::cout << "Queues and Tasks Status: stratum task queue - " << recvStratumMessages.size() << " | solutions queue - " << recvQubicSolutions.size()
            << " | active tasks - " << activeTasks.size() << std::endl;

        // TODO: replace the sleep below with condition_variable::wait_for(...) with the time and keepRunning.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        // TODO: handle connection error (threads will shut down on recv/send error and need to be restarted after connection is re-established)
    }

    // Close connection so that recv does not block the qubicRecvThread.
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
