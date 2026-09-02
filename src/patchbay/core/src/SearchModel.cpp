#include <WinJACKNexus/Patchbay/Core/SearchModel.hpp>

#include <algorithm>
#include <cctype>

namespace WinJACKNexus::Patchbay::Core {

namespace {

std::string lowerCase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool matches(const std::string& value, const std::string& query)
{
    return lowerCase(value).find(query) != std::string::npos;
}

}

std::vector<std::string> SearchModel::matchingClientIds(const GraphModel& model, const std::string& query)
{
    const auto normalizedQuery = lowerCase(query);
    std::vector<std::string> results;
    for (const auto& client : model.snapshot().clients) {
        if (normalizedQuery.empty() || matches(client.name, normalizedQuery) || matches(client.alias, normalizedQuery))
            results.push_back(client.id);
    }
    return results;
}

std::vector<std::string> SearchModel::matchingPortIds(const GraphModel& model, const std::string& query)
{
    const auto normalizedQuery = lowerCase(query);
    std::vector<std::string> results;
    for (const auto& port : model.snapshot().ports) {
        if (normalizedQuery.empty() || matches(port.name, normalizedQuery) || matches(port.alias, normalizedQuery))
            results.push_back(port.id);
    }
    return results;
}

}