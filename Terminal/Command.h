#pragma once

#include <string>

namespace term
{
// Hands a URL to the user's default handler (normally the browser) at once,
// starting the platform's opener directly, and returns without waiting for it.
void openInBrowser(const std::string& url);
} // namespace term
