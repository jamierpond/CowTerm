#include "Trash.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

// The freedesktop.org trash specification: the file moves into
// $XDG_DATA_HOME/Trash/files/ and a matching .trashinfo record naming its
// original path and deletion time goes into ../info/. Writing both is what
// makes the delete undoable from the desktop's file manager — a bare move into
// the directory leaves an entry that cannot be restored.
namespace term
{
namespace fs = std::filesystem;

namespace
{
fs::path trashRoot()
{
    if (const auto* dataHome = std::getenv("XDG_DATA_HOME");
        dataHome != nullptr && *dataHome != '\0')
        return fs::path {dataHome} / "Trash";

    const auto* home = std::getenv("HOME");

    if (home == nullptr || *home == '\0')
        return {};

    return fs::path {home} / ".local" / "share" / "Trash";
}

std::string deletionDate()
{
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());

    auto local = std::tm {};
    localtime_r(&now, &local);

    auto out = std::ostringstream {};
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

// Percent-encode everything outside the unreserved set: the spec's Path field
// is a URL-escaped fragment, so a name with a space or a '%' round-trips.
std::string percentEncode(const std::string& value)
{
    constexpr auto hex = "0123456789ABCDEF";
    auto out = std::string {};

    for (const auto character: value)
    {
        const auto byte = (unsigned char) character;

        const auto unreserved = (byte >= 'A' && byte <= 'Z')
                                || (byte >= 'a' && byte <= 'z')
                                || (byte >= '0' && byte <= '9')
                                || byte == '-' || byte == '_' || byte == '.'
                                || byte == '~' || byte == '/';

        if (unreserved)
        {
            out += character;
            continue;
        }

        out += '%';
        out += hex[byte >> 4];
        out += hex[byte & 0x0F];
    }

    return out;
}

// The trashed name must be unique against both directories at once, so a
// restore never picks up someone else's info record.
fs::path uniqueName(const fs::path& files,
                    const fs::path& info,
                    const fs::path& original)
{
    const auto stem = original.stem().string();
    const auto extension = original.extension().string();
    auto candidate = original.filename();

    for (auto counter = 1; counter < 10000; ++counter)
    {
        const auto trashInfo = info / (candidate.string() + ".trashinfo");

        if (!fs::exists(files / candidate) && !fs::exists(trashInfo))
            return candidate;

        candidate = stem + "." + std::to_string(counter) + extension;
    }

    return {};
}
} // namespace

bool moveToTrash(const std::string& path, std::string& error)
{
    auto code = std::error_code {};
    const auto source = fs::absolute(fs::path {path}, code);

    if (code)
    {
        error = "could not resolve path: " + code.message();
        return false;
    }

    if (!fs::exists(fs::symlink_status(source, code)))
    {
        error = "no such file: " + source.string();
        return false;
    }

    const auto root = trashRoot();

    if (root.empty())
    {
        error = "neither XDG_DATA_HOME nor HOME is set";
        return false;
    }

    const auto files = root / "files";
    const auto info = root / "info";

    fs::create_directories(files, code);
    fs::create_directories(info, code);

    if (code)
    {
        error = "could not create trash directory: " + code.message();
        return false;
    }

    const auto name = uniqueName(files, info, source);

    if (name.empty())
    {
        error = "could not find a free name in the trash";
        return false;
    }

    // The info record goes first: a crash between the two leaves a stale record
    // pointing at nothing, which is recoverable, where the reverse leaves an
    // unrestorable file.
    const auto record = info / (name.string() + ".trashinfo");
    auto stream = std::ofstream {record, std::ios::binary | std::ios::trunc};

    if (!stream)
    {
        error = "could not write trash info: " + record.string();
        return false;
    }

    stream << "[Trash Info]\n"
           << "Path=" << percentEncode(source.string()) << "\n"
           << "DeletionDate=" << deletionDate() << "\n";
    stream.close();

    if (!stream)
    {
        error = "could not write trash info: " + record.string();
        fs::remove(record, code);
        return false;
    }

    const auto destination = files / name;
    fs::rename(source, destination, code);

    if (!code)
        return true;

    // A rename cannot cross a filesystem boundary, and the home trash often is
    // one. Fall back to a copy the spec allows, and only unlink once it lands.
    if (code == std::errc::cross_device_link)
    {
        auto copyCode = std::error_code {};
        fs::copy(source,
                 destination,
                 fs::copy_options::recursive
                     | fs::copy_options::copy_symlinks,
                 copyCode);

        if (!copyCode)
        {
            fs::remove_all(source, copyCode);

            if (!copyCode)
                return true;
        }

        code = copyCode;
        fs::remove_all(destination, copyCode);
    }

    error = "could not move to trash: " + code.message();
    fs::remove(record, code);
    return false;
}
} // namespace term
