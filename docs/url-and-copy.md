# URLs and copy across wrapped lines

Long URLs and commands that the terminal wrapped onto a second row used to
copy with a newline in the middle, and a URL written as plain text could not
be opened. Now copy joins soft-wrapped rows, and Cmd-click (Ctrl-click off
macOS) opens any URL, wrapped or not, with a hover underline while the
modifier is held.

## What iTerm2 and Ghostty do

**Copy.** Both keep a per-row flag for "this row ran off the end" (iTerm2's
`EOL_SOFT` vs `EOL_HARD` in `iTermTextExtractor`, Ghostty's row `wrap` flag
used by `Screen.selectionString`). A wrapped row joins the next with no
newline; a real line end becomes `\n` and loses its trailing blanks.

**URL detection.** iTerm2 (`iTermURLActionFactory`) takes the run of
URL-legal characters around the clicked cell, walking across soft wraps (and
past its capture window for very long URLs), then finds the URL inside it and
trims surrounding punctuation and brackets. Its URL characters: letters,
digits and `.?\/:;$%=&_-,+~#@!*'()|[]`. Ghostty uses one regex
(`src/config/url.zig`): a scheme list, `[\w\-.~:/?#@!$&*+,;=%]+`, no trailing
`.` or `,`, and a trailing `)` only when the URL has a matching `(`
(`Rust_(video_game)` keeps it, `(https://x)` drops it). Ghostty also matches
bare file paths.

**OSC 8.** Both store an id per cell; the link is the run of cells carrying
it, across wraps.

## What CowTerm adopts

- Copy: Ghostty/iTerm2 behaviour, using the `Line::wrapped` flag CowTerm
  already keeps for reflow. A wrapped row keeps a space that ended it.
- URLs: iTerm2's "run of URL characters around the cell, across soft wraps",
  with Ghostty's character set plus `()[]`, and its trailing rules: drop
  `.,;:!?` and an unmatched `)` or `]`. `'` is not a URL character, so a
  quoted URL stops at its quote. Only schemes CowTerm already opens count
  (`http`, `https`, `mailto`, `file`). A logical line is read up to 64 rows
  either way.
- OSC 8: was already parsed and opened on Cmd-click; it now spans wrapped
  rows and underlines on hover like text URLs. An explicit link wins over
  text detection.
- Not adopted: bare file paths (Ghostty) and iTerm2's semantic history
  (opening files in an editor, smart selection rules).
