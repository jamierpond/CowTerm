# In dependency order: each of these links the one above it.
#
#   CowTermCore       the emulator model — parser, screen, PTY. No GPU,
#                     no daemon, no config file; testable without a window.
#   CowTermTerminal   the embeddable GPU terminal widget — what another
#                     project links to put a terminal pane in its window.
#
# Everything around them — sessions, splits, the palette, the daemon client,
# the tray — is the CowTerm app and stays in Terminal/.
add_subdirectory(CowTermCore)
add_subdirectory(CowTermTerminal)
