# The real game, as a reference

These scripts run the player's own copy of Half-Life (the Steam install,
native Linux build) without a screen, put the player at a place, drive the
game with a script, and take its pictures -- to hold the port's against.

Nothing of the player's is changed. The game is run with a folder of its own
beside `valve`, `hlds_ref`, which `setup.sh` makes (a `liblist.gam` that
falls back to `valve` for everything) and `remove.sh` deletes: the game's
config, saves and anything else it writes go there. (The engine's console
log, `qconsole.log`, is written in the game's own folder while it runs, and
removed when the run is over.)

    tools/ref/setup.sh
    tools/ref/play.sh c1a0 "-768 104 -180 0" "key a 0.1; sleep 0.15; shot /tmp/blow.png"
    tools/ref/compare.sh scientist c1a0 "-768 104 -180 0" "shot; key a 0.1; sleep 0.15; shot; sleep 1; shot"

- **Where the player starts.** The game has no command that puts the player
  somewhere, so `place.py` makes a copy of the map in `hlds_ref/maps` with its
  `info_player_start` moved and turned (the copy is deleted afterwards). The
  four numbers are the port's `EMU_START`: x, y, z and the way faced.
- **What the player has.** Every weapon (the game's own cheat), the crowbar
  in hand, and nothing can hurt them.
- **The script** is `tools/emu/xin.py`'s, the same that drives the port in
  the emulator: `sleep <s>`, `key <name> <s>`, `shot <png>`, `burst <dir>
  <n>`. `a` is attack on both sides; the arrows walk and turn; F6, F7 and F9
  take the crowbar, the pistol and the shotgun in the real game; `4`, `5`
  and `6` the hand grenade, the MP5 and the .357; `7`, `8`, `9` and `0` the
  crossbow, the satchel, the tripmine and the snark; `1`, `2`, `3` and `h`
  the RPG, the gauss, the egon and the hornetgun; `m` makes a zombie
  where the player stands there (the game's `give`); `z` the second trigger (R on
  the console), `t` the real game's reload (the port's is a tap on its
  RELOAD button, `tap 225 257`, and its weapons a tap on the slot of their
  kind, as the game's 1 to 5: `tap 28 422` for the first and 50 more for
  each one after, a second tap the next weapon of that kind).
- **How bright it is.** The side folder's game is given the player's own
  video settings (`brightness`, `gamma`, `gl_overbright`, `texgamma`,
  `lightgamma`, read from `valve/config.cfg` and never written back), so
  that what is compared with is the game as its owner sees it. F10 is the
  flashlight there (Select, `e`, in the port).
- **Measuring.** `measure.py <name> <map> "<x y z yaw>"...` photographs both
  sides at each place and reduces each picture to a 4x3 grid of mean
  colours over the part with no weapon or numbers in it; the pairs go to
  `build/ref/measure_<name>.csv`, and what is printed is how bright the
  port is beside the game, by how bright the game is there. (A place where
  the game's menu came up instead of the map shows as a row of black: look
  at `build/ref/<name>_NN.png` before believing a table.)
- **The sound.** `REF_AUDIO=<file>` writes the game's sound to a file instead
  of playing it (raw 16-bit stereo, about 5% slow against the clock).
  `REF_BARE=1` keeps only the world and the start in the map's copy, so that
  nothing is heard but the player; `REF_KEEP=<pattern>` keeps those classes too.
  Giving the player everything sets off the suit's speech: wait about 25 seconds
  before listening.
- **The pictures** are of an 800x600 screen with the game's 640x480 in the
  middle (`compare.sh` cuts it out). They are the game's own pictures: keep
  them under `build/`, not in the repository.
- **Needs**: Steam running (the game asks it who is playing), `Xvfb`,
  ImageMagick. The game says at every start that it has not the locale it
  wants; the script clicks that away.
- **Sound** is not captured: what the game sounds like is read out of its
  source (`helpSrc/`).
