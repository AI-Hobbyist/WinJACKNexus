/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
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