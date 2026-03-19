#include "stratum_recv_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>

#include "log.h"
#include <chrono>
#include <atomic>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"
#include "hash_util/hash_util.h"

constexpr unsigned int stratumReconnectBaseDelaySec = 2;
constexpr unsigned int stratumReconnectMaxDelaySec = 60;

bool initStratumProtocol(
    Connection& connection,
    ConcurrentQueue<nlohmann::json>& recvStratumMessages,
    std::vector<uint8_t>& extraNonce1,
    std::atomic<unsigned int>& extraNonce2NumBytes,
    const std::string& workerName,
    const std::string& workerPassword
)
{
    // Send mining.subscribe
    connection.sendMessage("{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n"); // Stratum messages must end in newline
    std::string subscribeResponse = connection.receiveResponse();
    if (subscribeResponse.empty())
    {
        ERR() << "No response received from pool for mining.subscribe." << std::endl;
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
        LOG() << "Received extraNonce1 " << extraNonce1String << ", size of extraNonce2 in bytes: " << extraNonce2NumBytes << std::endl;
    }
    else
    {
        ERR() << "Mining subscribe response could not be parsed." << std::endl;
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
                ERR() << "Mining authorization did not work." << std::endl;
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

void stratumReceiveLoop(
    std::stop_token st,
    ConcurrentQueue<nlohmann::json>& queue,
    Connection& connection,
    const PoolConfig& poolConfig,
    std::vector<uint8_t>& extraNonce1,
    std::atomic<unsigned int>& extraNonce2NumBytes
)
{
    std::string networkBuffer;
    std::array<char, 256> tempBuffer;

    while (!st.stop_requested())
    {
        int bytesRead = connection.receiveResponse(tempBuffer.data(), tempBuffer.size());

        if (bytesRead <= 0)
        {
            ERR() << "stratumReceiveLoop: Stratum connection lost." << std::endl;
            connection.closeConnection();
            networkBuffer.clear();

            // Reconnect with exponential backoff.
            unsigned int delaySec = stratumReconnectBaseDelaySec;
            while (!st.stop_requested())
            {
                LOG() << "stratumReceiveLoop: Reconnecting to pool in " << delaySec << "s..." << std::endl;
                for (unsigned int elapsed = 0; elapsed < delaySec && !st.stop_requested(); ++elapsed)
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                if (st.stop_requested())
                    return;

                if (connection.openConnection(poolConfig.url, poolConfig.stratumPort))
                {
                    if (initStratumProtocol(connection, queue, extraNonce1, extraNonce2NumBytes,
                            poolConfig.workerName, poolConfig.workerPassword))
                    {
                        LOG() << "stratumReceiveLoop: Reconnected to pool." << std::endl;
                        break;
                    }
                    connection.closeConnection();
                }

                ERR() << "stratumReceiveLoop: Reconnect failed." << std::endl;
                delaySec = (std::min)(delaySec * 2, stratumReconnectMaxDelaySec);
            }
            continue;
        }

        networkBuffer.append(tempBuffer.data(), bytesRead);

        // Each json object is separated by newline in the stratum protocol.
        size_t pos;
        while ((pos = networkBuffer.find('\n')) != std::string::npos)
        {
            std::string line = networkBuffer.substr(0, pos);
            if (!line.empty())
            {
                try
                {
                    queue.push(nlohmann::json::parse(std::move(line)));
                }
                catch (std::exception e) {}
            }

            // Erase processed string including the newline char.
            networkBuffer.erase(0, pos + 1);
        }
    }
}
