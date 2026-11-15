# In dependency order: each of these links only the ones above it.
#
#   CowTermEmulator      bytes in, screen state out: parser, screen, reflow.
#                        No process, no GPU, no daemon, no config file.
#   CowTermShell         the process behind a pane: the PTY and the Shell
#                        interface a pane talks to.
#   CowTermMultiplexer   panes that outlive the app: the session daemon's
#                        protocol and client, remote hosts over ssh, and what
#                        the daemon, `ct` and the app share. Emulator + Shell.
#   CowTermTerminalView  the embeddable GPU terminal widget — what another
#                        project links to put a terminal pane in its window.
#                        Emulator + Shell; never Multiplexer.
#
# The app's own UI — sessions and splits on screen, the palette, the switcher,
# popups, the tray — stays in Terminal/.
add_subdirectory(Emulator)
add_subdirectory(Shell)
add_subdirectory(Multiplexer)
if (NOT COWTERM_HEADLESS)
    add_subdirectory(TerminalView)
endif ()
