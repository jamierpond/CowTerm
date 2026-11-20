#pragma once

#include "GitHubPages.h"

#include <functional>
#include <string>

namespace term::github
{
// Runs the repository query for the pane's folder where the pane lives (here
// when its host is empty, otherwise over ssh) on a background thread, then, on the main
// thread, opens the chosen page in this machine's browser or hands the reason
// there is none to onFailure.
void openPageForFolder(const PaneFolder& folder,
                       PageKind kind,
                       std::function<void(const std::string& reason)> onFailure);
} // namespace term::github
