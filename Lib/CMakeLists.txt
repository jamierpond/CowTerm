# In dependency order: each of these links only the ones above it.
#
#   CowTermEmulator      bytes in, screen state out: parser, screen, reflow.
#                        No process, no GPU, no daemon, no config file.
#   CowTermShell         the process behind a pane: the PTY and the Shell
#                        interface a pane talks to.
#   CowTermTerminalView  the embeddable GPU terminal widget — what another
#                        project links to put a terminal pane in its window.
#
# Everything around them — sessions, splits, the palette, the daemon client,
# the tray — is the CowTerm app and stays in Terminal/.
add_subdirectory(Emulator)
add_subdirectory(Shell)
if (NOT COWTERM_HEADLESS)
    add_subdirectory(TerminalView)
endif ()
