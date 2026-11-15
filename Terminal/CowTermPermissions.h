#pragma once

#include "Config.h"

#include <PermissionsPanel/PermissionsPanel.h>

#include <vector>

// The macOS privacy grants (TCC) CowTerm asks for. A terminal needs them less
// for itself than for what it runs: macOS charges a command to the app that
// hosts its shell, so `osascript` or a shell reading
// ~/Library/Mail get CowTerm's grants or none.
namespace term::permissions
{
// Every grant, in the order the panel lists them.
const std::vector<pond::permissions::Info>& getCowTermGrants();

// CowTerm > Permissions…: CowTerm's grants in its configured font and theme.
pond::PermissionsPanelSettings makePermissionsPanelSettings(const AppConfig& config);

// Quits (the daemon keeps every shell) and opens CowTerm again.
void relaunchCowTerm();
} // namespace term::permissions
