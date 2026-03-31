#pragma once

#include <string>
#include <vector>
#include <optional>

#include <nlohmann/json.hpp>

/**
 * @brief Configuration for the stratum mining pool connection.
 */
struct PoolConfig
{
    std::string url;
    std::string stratumPort;
    std::string workerName;
    std::string workerPassword;
};

/**
 * @brief Configuration for Qubic network connections.
 */
struct QubicConfig
{
    std::vector<std::string> ips;
    int port = 21841;
};

/**
 * @brief Configuration for the test dispatcher's dummy stratum behavior.
 */
struct TestDispatcherConfig
{
    unsigned int timeBetweenJobsSec = 10;
    unsigned int frequencyClearJobs = 6;
};

/**
 * @brief Configuration for the dispatcher's identity (seed for signing tasks).
 */
struct DispatcherIdentityConfig
{
    std::string seed; // 55-character lowercase seed (a-z)
};

/**
 * @brief Configuration for the main dispatcher application.
 */
struct DispatcherAppConfig
{
    PoolConfig pool;
    QubicConfig qubic;
    DispatcherIdentityConfig identity;
    std::string statsFile; // optional path to write JSON stats (empty = disabled)
    bool dryRun = false;   // dry mode: connect to stratum + network but don't distribute tasks or process solutions
};

/**
 * @brief Configuration for the test dispatcher application.
 */
struct TestDispatcherAppConfig
{
    QubicConfig qubic;
    DispatcherIdentityConfig identity;
    TestDispatcherConfig testDispatcher;
};

/**
 * @brief Configuration for the test miner application.
 */
struct TestMinerAppConfig
{
    QubicConfig qubic;
    DispatcherIdentityConfig identity;
};

/**
 * @brief Load and parse a JSON config file.
 * @param filePath Path to the JSON config file.
 * @return The parsed JSON object, or std::nullopt if loading failed.
 */
std::optional<nlohmann::json> loadConfigFile(const std::string& filePath);

/**
 * @brief Parse a DispatcherAppConfig from a JSON object.
 */
DispatcherAppConfig parseDispatcherConfig(const nlohmann::json& j);

/**
 * @brief Parse a TestDispatcherAppConfig from a JSON object.
 */
TestDispatcherAppConfig parseTestDispatcherConfig(const nlohmann::json& j);

/**
 * @brief Parse a TestMinerAppConfig from a JSON object.
 */
TestMinerAppConfig parseTestMinerConfig(const nlohmann::json& j);
