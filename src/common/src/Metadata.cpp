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
#include <WinJACKNexus/Common/Metadata.hpp>

#include <cctype>

namespace WinJACKNexus::Common
{

namespace
{

void appendEscaped(std::string &output, std::string_view value)
{
    output.push_back('"');
    for (const auto character : value)
    {
        switch (character)
        {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20)
                output += "?";
            else
                output.push_back(character);
            break;
        }
    }
    output.push_back('"');
}

class Parser final
{
  public:
    explicit Parser(std::string_view input) : input_(input) {}

    std::optional<ModuleMetadata> parse()
    {
        ModuleMetadata metadata;
        skipWhitespace();
        if (!consume('{'))
            return std::nullopt;

        skipWhitespace();
        if (consume('}'))
        {
            skipWhitespace();
            return position_ == input_.size() ? std::optional<ModuleMetadata>(metadata)
                                              : std::nullopt;
        }

        for (;;)
        {
            const auto key = parseString();
            if (!key || !consume(':'))
                return std::nullopt;

            if (*key == "name")
            {
                const auto value = parseString();
                if (!value)
                    return std::nullopt;
                metadata.name = *value;
            }
            else if (*key == "category")
            {
                const auto value = parseString();
                if (!value)
                    return std::nullopt;
                metadata.category = *value;
            }
            else if (*key == "version")
            {
                const auto value = parseString();
                if (!value)
                    return std::nullopt;
                metadata.version = *value;
            }
            else if (*key == "author")
            {
                const auto value = parseString();
                if (!value)
                    return std::nullopt;
                metadata.author = *value;
            }
            else if (*key == "description")
            {
                const auto value = parseString();
                if (!value)
                    return std::nullopt;
                metadata.description = *value;
            }
            else if (*key == "dependencies")
            {
                const auto values = parseStringArray();
                if (!values)
                    return std::nullopt;
                metadata.dependencies = *values;
            }
            else if (!skipValue())
            {
                return std::nullopt;
            }

            skipWhitespace();
            if (consume('}'))
            {
                skipWhitespace();
                return position_ == input_.size() ? std::optional<ModuleMetadata>(metadata)
                                                  : std::nullopt;
            }
            if (!consume(','))
                return std::nullopt;
        }
    }

  private:
    void skipWhitespace()
    {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0)
            ++position_;
    }

    bool consume(char character)
    {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != character)
            return false;
        ++position_;
        return true;
    }

    std::optional<std::string> parseString()
    {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"')
            return std::nullopt;
        ++position_;

        std::string result;
        while (position_ < input_.size())
        {
            const char character = input_[position_++];
            if (character == '"')
                return result;
            if (character != '\\')
            {
                result.push_back(character);
                continue;
            }
            if (position_ >= input_.size())
                return std::nullopt;
            switch (input_[position_++])
            {
            case '\\':
                result.push_back('\\');
                break;
            case '"':
                result.push_back('"');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<std::vector<std::string>> parseStringArray()
    {
        if (!consume('['))
            return std::nullopt;
        std::vector<std::string> result;
        skipWhitespace();
        if (consume(']'))
            return result;

        for (;;)
        {
            const auto value = parseString();
            if (!value)
                return std::nullopt;
            result.push_back(*value);
            skipWhitespace();
            if (consume(']'))
                return result;
            if (!consume(','))
                return std::nullopt;
        }
    }

    bool skipValue()
    {
        skipWhitespace();
        if (position_ >= input_.size())
            return false;
        if (input_[position_] == '"')
            return parseString().has_value();
        if (input_[position_] == '[')
        {
            int depth = 0;
            do
            {
                if (input_[position_] == '[')
                    ++depth;
                if (input_[position_] == ']')
                    --depth;
                ++position_;
            } while (position_ < input_.size() && depth > 0);
            return depth == 0;
        }
        const auto start = position_;
        while (position_ < input_.size() && input_[position_] != ',' && input_[position_] != '}')
            ++position_;
        return position_ > start;
    }

    std::string_view input_;
    std::size_t position_{};
};

}

std::string ModuleMetadata::toJson() const
{
    std::string result = "{\"name\":";
    appendEscaped(result, name);
    result += ",\"category\":";
    appendEscaped(result, category);
    result += ",\"version\":";
    appendEscaped(result, version);
    result += ",\"author\":";
    appendEscaped(result, author);
    result += ",\"description\":";
    appendEscaped(result, description);
    result += ",\"dependencies\":[";
    for (std::size_t index = 0; index < dependencies.size(); ++index)
    {
        if (index != 0)
            result.push_back(',');
        appendEscaped(result, dependencies[index]);
    }
    result += "]}";
    return result;
}

std::optional<ModuleMetadata> ModuleMetadata::fromJson(std::string_view json)
{
    return Parser(json).parse();
}

}