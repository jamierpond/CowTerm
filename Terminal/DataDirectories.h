#pragma once

#include <PondApp/DataDirectory.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Everything CowTerm keeps lives in pond/CowTerm (see pondapp): the app's
// sessions, the daemon's panes, the usage logs. A named instance
// ($COWTERM_INSTANCE) gets its own sessions and panes folders.
namespace term
{
std::filesystem::path getDataDirectory();
std::filesystem::path getLogsDirectory();
std::filesystem::path getSessionStoreDirectory();
std::filesystem::path getPaneStoreDirectory();

// Where CowTerm kept the app's data before pond/CowTerm, moved there on the
// app's first launch. The daemon's panes move separately, when the daemon
// starts, so a daemon already running keeps writing where it always has.
std::vector<pondapp::DataDirectoryMove> moveOldAppData();
std::optional<pondapp::DataDirectoryMove> moveOldPaneStore();
} // namespace term
