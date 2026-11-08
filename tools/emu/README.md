# Emulator test helpers

Scripts that run the ROM in melonDS without a display, press buttons, and take
screenshots. They are for development; nothing here is needed to build or play.

Requirements: melonDS, `xvfb-run`, ImageMagick (`import`), Python 3 with the
X11 and XTest libraries present (`libX11.so.6`, `libXtst.so.6`).

```sh
export MELONDS=/path/to/melonDS      # if it is not on PATH
tools/emu/setup.sh                   # once: private emulator config in build/emu/
tools/emu/play.sh ds "sleep 9;shot /tmp/a.png"
```

`setup.sh` copies your melonDS configuration to `build/emu/` and points the
copies' SD card at this repository's `sdroot/`, with card images of their own.
Your own configuration and card images are never written to.

`play.sh <ds|dsi> "<script>"` runs the ROM and feeds it a script of commands
separated by `;`. melonDS is given a copy of the ROM marked as homebrew
(`build/emu/<name>`, game code `####`): the game's own code, WHLD, gets no
card from it. `tools/emu/emurom.py` makes the copy and sets the recent list
to it and the child.

| command | |
|---|---|
| `sleep <s>` | wait |
| `ready [s]` | wait until the game is on screen (the emulator can take half a minute over the card first; at most `s` seconds, 90 if not given) |
| `key <keysym> <s>` | hold a key (your melonDS key mapping applies) |
| `hold <keysym>`, `release <keysym>` | press and let go of a key across other commands: two buttons at once |
| `drag <x0> <y0> <x1> <y1>` | stylus drag, in window pixels |
| `tap <x> <y>` | a quick touch (two close together are a jump) |
| `sweep <s> <px per step>` | hold the stylus and sweep left-right for a time |
| `shot <file>` | screenshot of both screens |
| `burst <dir> <n>` | n screenshots as fast as they come |
| `sweepburst <dir> <n> <px>` | n screenshots while the view sweeps |

`EMU_AUDIO=<file>` before `play.sh` writes the emulator's sound to that file
instead of playing it: raw 16-bit stereo samples at 48 kHz (SDL's "disk" audio
driver), which can be measured, or listened to with
`ffplay -f s16le -ar 48000 -ac 2 <file>`.

`EMU_START="x y z yaw [track]"` starts the player at a place, armed
and wearing the suit, with the debug readout on: `track` starts that track of
the music (-1 for none).

The private card images are a gigabyte each (`setup.sh` says so to melonDS):
a whole cooked campaign is a quarter of that, and an image that is full
silently leaves new files out. If something on `sdroot/` is "not on the
card" in the emulator, delete `build/emu/cfg_*/*.bin*` and run `setup.sh`
again.

The window is 256 wide; the top screen starts at about y = 55 and the touch
screen at about y = 247.

`EMU_START` may have a fifth number (a music track to start with; -1 for
none) and a sixth: the seconds after which the player dies, once -- for
seeing which save dying goes back to (`EMU_START="1120 2112 836 180 -1 50"`).

## Two consoles

`EMU_INSTANCES=2 tools/emu/play.sh ds "<script>"` runs two consoles linked by
local wireless, side by side on a 640x480 screen: melonDS's second instance
(System > Multiplayer > Launch new instance), started on the same ROM from
its recent list before the script begins.

- The first console is on the left, its touch screen from (0,222); the second
  at the right, its touch screen from (320,222). (So a touch at (x,y) of the
  touch screen is `tap x 222+y` or `tap 320+x 222+y`.)
- `focus 0` / `focus 1` sends the keys to one or the other; `place <n> <x>
  <y>` moves a window; `windows` lists where they are.
- `debug_net.txt` on the card ("x y z yaw", whole units and degrees) puts the
  host there when a match starts and the client 160 units in front of it,
  facing it. Remove it afterwards.
- The two instances share the card folder, and both are called "melonDS".
- `EMU_ROM2=child` starts the second on the Download Play child
  (`child/out/hl-child.nds`, the second entry of the recent list in
  build/emu's configs) instead: a console sent the game, started directly.
  The emulator cannot send it by Download Play itself -- that needs a
  bootable 256 KB DS firmware, and the dumps here are a DSi's 128 KB one --
  so the child, finding no parameter from a host, looks for any game that
  lets consoles without the game in, and joins it. `EMU_PICK2="tap x y"`
  picks another entry of that list. A child plays no music (none is sent),
  and its map loads go dark (no loading picture: the frame-freeze bank is
  its ARM7's).
- `EMU_INSTANCES=3` runs a third console at x 640 on a 960x480 screen (its touch
  screen from (640,222)); `EMU_ROM3=child` starts it on the child. The three run
  slower, so allow more time in scripts, and `burst` files sort by number, not
  as text. The link dies with time here too: a child's second map is not reached.
- Keys are not to be relied on to change a setting with two consoles running
  (a press is seen twice, or not at all): a touch is, or a settings file made
  beforehand -- `tools/emu/mksettings.cpp` writes one with consoles without
  the game let in. Put the card's own back afterwards.

