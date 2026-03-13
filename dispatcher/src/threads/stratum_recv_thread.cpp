#include "stratum_recv_thread.h"

#include <thread>
#include <stop_token>
#include <iostream>

#include <nlohmann/json.hpp>

#include "concurrency/concurrent_queue.h"

void stratumReceiveLoop(std::stop_token st, ConcurrentQueue<nlohmann::json>& queue, Connection& connection)
{
    std::string networkBuffer;
    std::array<char, 256> tempBuffer;

    while (!st.stop_requested())
    {
        int bytesRead = connection.receiveResponse(tempBuffer.data(), tempBuffer.size());

        if (bytesRead <= 0)
        {
            std::cerr << "Connection closed or error occurred." << std::endl;
            break;
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