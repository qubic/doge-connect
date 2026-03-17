#include "config.h"

#include <fstream>
#include <iostream>

std::optional<nlohmann::json> loadConfigFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "Could not open config file: " << filePath << std::endl;
        return std::nullopt;
    }

    try
    {
        nlohmann::json j = nlohmann::json::parse(file);
        return j;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        std::cerr << "Failed to parse config file: " << e.what() << std::endl;
        return std::nullopt;
    }
}

static QubicConfig parseQubicConfig(const nlohmann::json& j)
{
    QubicConfig config;
    const auto& qubic = j.at("qubic");
    config.ips = qubic.at("ips").get<std::vector<std::string>>();
    config.port = qubic.value("port", 21841);
    return config;
}

DispatcherAppConfig parseDispatcherConfig(const nlohmann::json& j)
{
    DispatcherAppConfig config;

    const auto& pool = j.at("pool");
    config.pool.url = pool.at("url").get<std::string>();
    config.pool.stratumPort = pool.at("stratumPort").get<std::string>();
    config.pool.workerName = pool.at("workerName").get<std::string>();
    config.pool.workerPassword = pool.value("workerPassword", "123");

    config.qubic = parseQubicConfig(j);
    return config;
}

TestDispatcherAppConfig parseTestDispatcherConfig(const nlohmann::json& j)
{
    TestDispatcherAppConfig config;
    config.qubic = parseQubicConfig(j);

    if (j.contains("testDispatcher"))
    {
        const auto& td = j["testDispatcher"];
        config.testDispatcher.timeBetweenJobsSec = td.value("timeBetweenJobsSec", 10u);
        config.testDispatcher.frequencyClearJobs = td.value("frequencyClearJobs", 6u);
    }

    return config;
}

TestMinerAppConfig parseTestMinerConfig(const nlohmann::json& j)
{
    TestMinerAppConfig config;
    config.qubic = parseQubicConfig(j);
    return config;
}
