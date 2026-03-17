#include <memory>
#include <iostream>
#include <thread>
#include <cstdint>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#include <Winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketType;
#define INVALID_SKT INVALID_SOCKET
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SocketType;
#define closesocket close
#define INVALID_SKT -1
#endif

#include "config/config.h"
#include "concurrency/concurrent_queue.h"
#include "connection/qubic_connection.h"
#include "connection/connection.h"
#include "threads/input_thread.h"
#include "structs.h"

#include "task_recv_thread.h"
#include "mining_thread.h"
#include "mining_structs.h"

/**
 * @brief A test miner application to mine tasks from the Dispatcher.
 * 
 * The testminer starts listening on the specified port for a connection from the Dispatcher. 
 * Once the connection is established, it starts the following threads for the main loop of the application:
 * - inputThread: to react to key presses (currently supported: 'q' to quit).
 * - taskRecvThread: to receive tasks from the dispatcher.
 * - miningThread: to mine received tasks and send found solutions back to dispatcher.
 * 
 * @return 0 if the application completed without errors, 1 if the application terminated due to an error.
 */
int main(int argc, char* argv[])
{
    std::string configPath = "test_miner_config.json";
    if (argc > 1)
        configPath = argv[1];

    auto configJson = loadConfigFile(configPath);
    if (!configJson)
        return 1;

    TestMinerAppConfig config;
    try
    {
        config = parseTestMinerConfig(*configJson);
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

    // Wait for dispatcher to connect on specified g_qubicPort.

    SocketType serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SKT)
    {
        std::cerr << "Socket creation failed." << std::endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    serverAddr.sin_port = htons(config.qubic.port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Socket bind failed." << std::endl;
        closesocket(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 5) < 0)
    {
        std::cerr << "Socket listen failed." << std::endl;
        closesocket(serverSocket);
        return 1;
    }

    std::cout << "Test miner is listening on port " << config.qubic.port << "..." << std::endl;

    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    SocketType clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen); // blocks until someone connects

    if (clientSocket == INVALID_SKT)
    {
        std::cerr << "Accepting client connection failed." << std::endl;
        return 1;
    }

    char clientIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
    std::cout << "Connection accepted from " << clientIp << std::endl;

    // Send Qubic handshake data.
    std::vector<char> handshakeData(sizeof(RequestResponseHeader) + sizeof(ExchangePublicPeers));
    RequestResponseHeader* header = reinterpret_cast<RequestResponseHeader*>(handshakeData.data());
    header->setSize(handshakeData.size());
    header->setType(ExchangePublicPeers::type());
    send(clientSocket, handshakeData.data(), handshakeData.size(), 0);

    // Create a QubicConnection wrapper using the socket.
    QubicConnection connection{ Connection::Socket(clientSocket) };

    // - 1 thread for receiving tasks --> this needs to signal if cleanJobs flag was true
    // - 1 thread for mining and sending shares if found

    // Create a queue for received tasks.
    ConcurrentQueue<InternalMiningTask> activeTasks;

    std::atomic<bool> keepRunning = true; // Atomic flag to signal the rest of the app to stop.
    std::atomic<bool> startNewJob = false; // A flag for the taskRecvThread to signal the mining thread to start a new task when the jobs were cleared.
    std::atomic<uint64_t> solutionsFound = 0;

    // Start all other threads. std::ref is needed here to force a pass by reference for the shared variables.
    // Start the input thread to react to key presses (currently supported: 'q' to quit).
    std::jthread inputThread(inputThreadLoop, std::ref(keepRunning));
    // Start the taskRecvThread to receive tasks from the dispatcher.
    std::jthread taskRecvThread(taskReceiveLoop, std::ref(activeTasks), std::ref(connection), std::ref(startNewJob));
    // Start the miningThread to mine received tasks and send found solutions back to dispatcher.
    std::jthread miningThread(miningLoop, std::ref(activeTasks), std::ref(connection), std::ref(startNewJob), std::ref(solutionsFound));

    while (keepRunning)
    {
        std::cout << "Status: mining tasks waiting in queue - " << activeTasks.size() << " | solutions found - " << solutionsFound << std::endl;

        // TODO: replace the sleep below with condition_variable::wait_for(...) with the time and keepRunning.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        // TODO: handle connection error (threads will shut down on recv/send error and need to be restarted after connection is re-established)
    }

    // Request stop of the shareValidThread manually and push a dummy object onto the queue to break the blocking pop().
    miningThread.request_stop();
    activeTasks.push(InternalMiningTask{});

    // Close connection so that recv does not block the taskRecvThread.
    closesocket(clientSocket);
    closesocket(serverSocket);

    // The jthreads are joined automatically when they are destructed.

    return 0;
}