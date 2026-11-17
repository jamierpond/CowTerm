#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cowterm::mobile
{
// A QR code as its square of modules, row by row, true for a dark one. No
// quiet zone: whoever draws it leaves the margin.
struct QrCode
{
    int size = 0;
    std::vector<bool> darkModules;

    bool isDark(int column, int row) const { return darkModules[static_cast<std::size_t>(row * size + column)]; }
};

// From Core Image's QR generator, medium error correction. Empty if it fails.
std::optional<QrCode> makeQrCode(const std::string& text);
} // namespace cowterm::mobile
