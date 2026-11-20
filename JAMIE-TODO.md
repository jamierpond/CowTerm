# Things waiting on Jamie

Written 2026-11-18. Each item says what it unblocks.

## Phone: reach tamby again (it moved tailnets)

The fix is on main (the phone dials `jamies-mac-mini.tail8acf23.ts.net` and
still calls the machine tamby). pond was not reachable from big-cow when it was
ready, so it is not installed yet.

- [ ] Plug pond into big-cow and unlock it, then run
      `cd ~/projects/CowTerm-ios-app && just configure && just ios-device pond && just ios-pair pond`.
      It builds and installs the app, then re-pairs: the pairing output should
      say "the phone dials: jamiepond@tamby=jamies-mac-mini.tail8acf23.ts.net"
      and that all three hosts accept the phone's key. No authorized_keys line
      is added (the key is already there), and nothing on the Mac restarts.

## Push notifications: your phone rings when a Claude finishes or asks

macprorack is ready. `just push-relay-check-ready` shows 14 of 18 requirements
met; the other 4 depend on the steps below.

- [ ] 1. In the Apple developer portal, make an APNs key named "CowTerm Push"
      in the Cows In Love team and download the `.p8` file.
- [ ] 2. In your personal 1Password, make a vault "CowTerm Push" with an item
      "APNs Key" and three text fields: `key id`, `team id`, `private key`
      (the whole `.p8` text). Then delete the download.
- [ ] 3. Make a 1Password service account with read access to that vault only.
- [ ] 4. In `~/projects/CowTerm`, run `just push-relay-store-token` and paste
      the token when asked. The relay starts by itself.
- [ ] 5. Run `just push-relay-check-ready` and expect 17 of 18.
- [ ] 6. After the push-capable phone build is on pond (see TestFlight), run
      `just ios-set-push-relay pond`, then `just push-check-on-phone`. The
      phone should ring.

## TestFlight: Eyal can beta test

- [ ] Turn on 1Password > Settings > Developer > "Integrate with 1Password CLI".
- [ ] Confirm the Cows In Love team and registering the bundle id
      `audio.pond.cowterm.mobile`.
- [ ] Create the app record in App Store Connect by hand.
- [ ] Give Eyal's Apple ID email, and say whether he is an internal or an
      external tester.

## Restarts: say when

Nothing below happens until you say so.

- [ ] Relaunch the Mac app. Brings the removal of `Ctrl+A c` (`Cmd+N` still
      opens a new session).
- [ ] Restart the daemon on big-cow. Brings the unread bell marks and the
      "dv not found" fix. First run `just push-relay-set-host local big-cow`.
      A daemon restart ends every shell on that machine.
- [ ] tamby, once it stays on the network:
      `just install-headless-on-host jamiepond@tamby`,
      `just push-relay-set-host jamiepond@tamby tamby`, then restart its daemon.
- macprorack's daemon already runs the current build.

## Phone sitting: about ten minutes with pond in your hand

- [ ] Install the current build on pond (`just ios-device`).
- [ ] Check Ctrl+letter from the key row.
- [ ] Check held backspace. It is known not to repeat yet; confirm it is still
      wrong so it gets fixed against the real thing.
- [ ] Check a tap inside dv moves the cursor, and a full dv comment round:
      open the comment box, type, save, submit.
- [ ] Check the ☰ unread count and the square yellow dots on unread panes.

## macprorack's Wi-Fi

On 2026-11-18 its Wi-Fi driver hung at 05:43 after the router asked it to
change access point, and it stayed offline until you power-cycled it at 09:50.
Pick any of:

- [ ] Plug in Ethernet. Removes the problem.
- [ ] Have it ignore the router's steering requests and turn off Wi-Fi power
      saving (needs sudo; I script it in `~/.config`).
- [ ] Add a watchdog that reloads the driver after a few minutes offline
      (needs sudo; same).

## Questions

- [ ] Line-level usage build (see which code your normal day runs, to find
      code to delete) is built and on main. To start a week of counting, from
      `~/projects/CowTerm`: `just install` (the line-coverage build is its
      default for now), then `just restart-cowterm-on-this-mac` (ends the
      programs in your panes). Then `just coverage-report`, and
      `just install-without-coverage` to go back to the normal app.
- [ ] On the Mac, should clicking or scrolling in a pane count as looking at
      it, so its unread mark clears? Today only typing keeps a pane "viewed".
- [ ] Should the tray show unread sessions?
- [ ] This Mac's colour: is its one name `big-cow` or `daily-driver`?
- [ ] Should wide dv switch from red and green to blue and orange, as the
      narrow layout did?
- [ ] Did you restart the daemon on big-cow at 22:16 on 2026-11-17? Something
      did, and no agent claims it.
- [ ] Does `Ctrl+A i` (lazygit) still work since the app was relaunched with a
      clean environment?
