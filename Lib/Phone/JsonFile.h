#pragma once

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <Miro/Reflect.h>

#include <filesystem>
#include <optional>

namespace term::phone
{
// Creates the file's folder, then replaces the file in one step, so a crash
// mid-save leaves the old file. False when it could not be written.
template <typename Value>
bool writeJsonFileAtomically(const std::filesystem::path& path, const Value& value)
{
    auto ignoredError = std::error_code {};
    std::filesystem::create_directories(path.parent_path(), ignoredError);

    try
    {
        eacp::Files::writeFileAtomically(eacp::FilePath {path.string()}, Miro::toJSONString(value));
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// Empty when there is no file, or it does not parse.
template <typename Value>
std::optional<Value> readJsonFile(const std::filesystem::path& path)
{
    const auto text = eacp::Files::readFile(eacp::FilePath {path.string()});

    if (text.empty())
        return std::nullopt;

    try
    {
        auto value = Value {};
        Miro::fromJSONString(value, text);
        return value;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
} // namespace term::phone
