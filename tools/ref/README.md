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

## Physics, frame by frame

Pictures say little about how a box slides or a grenade flies. For that the
real game is run with the game code of a diagnostic build of the SDK,
[CrowbarDS/hlsdk-portable-diag](https://github.com/CrowbarDS/hlsdk-portable-diag),
which logs what moves, frame by frame; and the port plays the same thing on
the host, through its own game code, into a log of the same form.

    tools/ref/diag.sh push_straight
    DIAG_PORT_ONLY=1 tools/ref/diag.sh push_straight    # the port's side again

- **A scenario** (`tools/ref/diag/<name>.txt`) names a map and a start in its
  head (`# map c1a2`, `# start x y z yaw`, and `# keep <pattern>` for the
  classes kept besides the world, `func_.*` by default: no monsters). Each
  line after is a span of frames, `<frames> <forward> <side> <yaw> <pitch>
  [buttons]`: speeds as the keys give them (400), the view in degrees (pitch
  down positive, as the game's), buttons as letters (`a` attack, `b` the
  second, `u` USE, `j` jump, `d` duck, `r` reload).
- **The real game's side.** `REF_DIAG=1` (which `diag.sh` sets) makes
  `setup.sh` copy that build's `hl.so` and `client.so` into the side folder
  and point its `liblist.gam` at them; without it they are taken away again.
  The client plays the scenario in place of the keys, so each frame's input
  is exactly the script's; `host_framerate` makes every frame a sixtieth of a
  second, however slowly the game runs off-screen (`DIAG_STEP` for another).
  The log is `build/ref/diag/<name>_pc.csv`; the map as it was played, start
  moved and entities stripped, `build/ref/diag/<name>.bsp`.
- **The port's side.** `build/convert/diag_replay` (built with the
  converter) cooks that same map, stands the player still for two seconds,
  then plays the scenario in the console's order of things each frame (the
  logic and its boxes, USE, the monsters, the player's move, then the
  weapons), a sixtieth of a second each, into `build/ref/diag/<name>_ds.csv`.
- **Weapons.** Both players start as `play.sh` starts the game's: nothing
  hurts them (`god`), they have what `impulse 101` gives, the crowbar in
  hand. A scenario's `cmd` lines (`cmd weapon_rpg`, `cmd give ammo_357`,
  `cmd god` to be hurt again) are run on the port's side on the frame the
  game's log says it ran them. Each frame's random seed is the game's. Both
  logs then have the weapon in hand (`wpn`), each round fired (`shot`: from
  where, which way, the spread drawn and the cone), what was thrown or
  fired (`proj`), each blast (`boom`) and each hurt to the player (`dmg`,
  with the push it gave). The aim is the view's, without its kick: there
  are no monsters for the aim to bend to (`sv_aim 0` on the game's side).
- **Holding them together.** `diagcmp.py <name>` says, for the player and
  each box that moved, from which frame the two part, by how much at worst,
  and where each ends; and writes both side by side to
  `build/ref/diag/<name>_cmp.csv`. (The real game moves its boxes after the
  player in a frame, the port before: a box's place at the end of a frame is
  read from each log accordingly.) On a ladder it says on which frames each
  side climbed and which ladder, and holds the face each found (its normal),
  solid underfoot, and the climb asked for against each other (`lad_*`).
  For weapons it says when each was taken out, which frames fired and how
  the rounds went, and pairs what was thrown or fired by kind, in the order
  they appeared: where each was let go, from which of its frames the paths
  part, where each came to rest, and the blasts and the hurts (`wpn_*`).
- **Turning brushes.** A fan has been turning since its map began, from a
  moment the two sides do not share: the replay sets each `func_rotating`
  to the angle and speed the game's log has for it on the first frame, and
  both logs then have it every frame (`rot`), held together by diagcmp
  (`fan_*`).
- **The console's pace.** `DIAG_VBLANKS=2` plays the port's side two of the
  game's frames at a time, as the console's 30 a second does, compared on
  every second frame.
- **A start in the air** is not one in the game, which drops the player to
  the floor before the script begins; and one inside a brush leaves the
  game's player stuck where the port's gets out: a scenario starts on the
  floor, in the open.
- **Building the SDK fork**: next to this repository, 32-bit (the Steam game
  is), with `gcc-multilib`, `g++-multilib` and `libsdl2-dev:i386`:

      cmake -S ../hlsdk-portable-diag -B ../hlsdk-portable-diag/build -G Ninja \
          -D64BIT=OFF -DGOLDSOURCE_SUPPORT=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
      cmake --build ../hlsdk-portable-diag/build

  (Not Release: that adds `-funsafe-math-optimizations`, and the game's own
  is to be matched. `HLDS_DIAG_BUILD` says where the build is, if elsewhere.)
- The logs are measurements of the player's own game: they stay in `build/`.
