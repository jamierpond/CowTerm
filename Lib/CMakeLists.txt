# In dependency order: each of these links only the ones above it.
#
#   CowTermEmulator      bytes in, screen state out: parser, screen, reflow.
#                        No process, no GPU, no daemon, no config file.
#   CowTermShell         the process behind a pane: the PTY and the Shell
#                        interface a pane talks to.
#   CowTermMultiplexer   panes that outlive the app: the session daemon's
#                        protocol and client, remote hosts over ssh, and what
#                        the daemon, `ct` and the app share. Emulator + Shell.
#   CowTermPush          when a Claude's finishing or asking rings the phone,
#                        and the notification it becomes. Core only.
#   CowTermPushRelay     `ct push-relay`: device tokens, ES256-signed requests
#                        to Apple's push service. Push + mbedTLS.
#   CowTermPhone         the phone's link to one host's daemon over the
#                        in-process ssh client: pane choice, link state,
#                        stored last screen. Multiplexer + EmbeddedSsh.
#   CowTermFolderWatch   a folder and everything under it watched with the
#                        platform's change events, for the daemon. eacp core
#                        only, written to move into eacp one day.
#   CowTermTerminalView  the embeddable GPU terminal widget — what another
#                        project links to put a terminal pane in its window.
#                        Emulator + Shell; never Multiplexer.
#
# The app's own UI — sessions and splits on screen, the palette, the switcher,
# popups, the tray — stays in Terminal/.
add_subdirectory(Emulator)
add_subdirectory(Shell)
add_subdirectory(Multiplexer)
add_subdirectory(Push)
if (NOT IOS)
    add_subdirectory(PushRelay)
    add_subdirectory(FolderWatch)
endif ()
if (TARGET CowTerm::EmbeddedSsh)
    add_subdirectory(Phone)
endif ()
if (NOT COWTERM_HEADLESS)
    add_subdirectory(TerminalView)
endif ()
