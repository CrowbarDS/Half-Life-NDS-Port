# Half-Life NDS Port: technical notes

What the port does and how, in detail: what is drawn and how it is made to
fit, how the game's rules were carried over, what the converter writes, and
the tools for measuring and comparing. The [README](../README.md) says how to
build and run it; this is for whoever wants to know why something is the way
it is before changing it.

- [What works](#what-works)
- [The game as a library](#the-game-as-a-library)
- [Not yet](#not-yet)
- [Tests, the DSP program, timing](#tests-the-dsp-program-timing)
- [Comparing with the game](#comparing-with-the-game)
- [What the converter writes](#what-the-converter-writes)

## What works

Milestone 11, "levels of detail".

- Any of the game's maps converts to a cooked level: BSP tree, PVS, clip hulls,
  and the map's faces as ready-made runs of geometry-engine commands with their
  lightmaps baked into vertex colours -- as the game shows them: a third
  stronger than the file has them and lifted by the power 0.8, cut off at
  full (`--light-scale`, `--light-power` for another curve), with its
  textures lifted by the same power. That curve was not read out of
  anything: it was fitted to the real game photographed beside the port at
  the same places (`tools/ref/measure.py`), which brought the mean
  difference over 120 patches of wall on three maps from 15.6 to 5.6 of
  255 (the port now 5% under the game, where it was 18% under).
  Neighbouring faces share vertices in
  quad strips, and faces are merged only where that provably changes nothing
  on screen (about 12% fewer vertices over the campaign).
- World textures in the DS's 4x4 compressed format; textures with holes
  (grates, fences) in A3I5, with soft edges. Both are sized per map to fit the
  texture and palette banks. Unless cooked with `--no-stream-textures`,
  the plain opaque ones are cooked at the game's own size (up to 128 on a side,
  where they were halved) and read in from the card as they come into view;
  see "Streaming textures" below.
- The DS renders the world with PVS, frustum and back-face culling
  against a polygon and vertex budget, in one pass, at 60 fps. The converter
  works the holes between the BSP's leaves out again from its tree (a .bsp no
  longer has them: `Portals.cpp`) and floods through them from every leaf,
  looking every way (`shared/HlPortals.hpp`: never less than what is seen --
  tested against lines of sight on the game's maps): what a map's worst view
  is as it is seen, how far it can be seen, and its worst places (`--hotspots`).
  The flood is not done on the console: a pass of it cost about 3 ms on a DSi
  and left out little of what a pass short of room would have cut. A surface
  is never made of faces of two leaves, so that it is drawn only where its
  own leaf is seen (7 to 13% more surfaces). Brush entities that are solid --
  doors, lifts, trains -- are drawn nearest first with all they cost set aside, and once one does not fit
  nothing farther is: a near door is never left out for a far one
  (`shared/HlBrushPlan.hpp`); a list too short for all in view leaves out the
  farthest, and what is left out is counted (the overlay's `cut s b`, perf.txt).
  Each map is drawn no farther than it can be seen from anywhere in it (the
  converter's flood from every leaf: 1536 to 8192 units), the fog whole by
  then. What a view asks for beyond the budget is left out from the far end,
  and a map the converter judged HEAVY (its worst view as it is seen -- from
  every leaf a player's eye can be in, eight ways round and three up and
  down, through the portals -- is over a pass's budget) has the fog on always,
  at the player's distance, unless the fog is set to off or to "always" already.
  In the others it comes in only where a view is cut (see "Fog" below). Back faces
  are rejected on the CPU, surface by surface; the hardware's own test is off,
  because it goes by a polygon's first three vertices and misjudges very thin
  ones; the converter orders every polygon so that those three make a real corner.
- A model in view that is behind a wall is not drawn: a line from the eye to
  its middle, top and bottom, a few a frame, kept per model (hidden after
  two looks in a row, seen again at the first clear one).
- On a DSi the world's surfaces go to the geometry engine as one display
  list, fed by the DSi's own DMA (NDMA) while the processor works out where
  the models' bones are. (Not while the DSP option is on: a DMA to the
  geometry engine and the DSP's traffic do not get on.) NEA's own display
  lists use NDMA there too (`NEA_DisplayListEnableNDMA`).
- A view with too much in it loses what is least seen first. Pressure (0 to
  3; `pN` on the overlay) rises while passes run out of budget and leaves
  out, more with each step, world surfaces and brush entities only a few
  pixels across and all but the nearest models. Only what is still too much
  is cut off at a distance, and the hardware's fog is brought in just short
  of that distance, so the far end fades out instead of ending in a hole.
  A view that fits has neither.
- Brush entities are drawn and solid: opaque, cut-out, see-through (glass),
  flat-colour, and invisible-but-solid.
- Doors (sliding and hinged), buttons, lifts and trains on path corners,
  ladders, walls that switch on and off, and the wiring between them:
  triggers, relays, multi_managers, multisources, delays and killtargets.
  Moving brushes carry and push the player, and turn back when blocked.
- Maps chain: level changes with landmarks carry the player (and the tram
  or lift they are riding) into the next map. The opening tram ride runs from c0a0 to
  the station in c0a0e by itself, platforms that lower the tram included.
  A lift goes on along the next map's own path, even where the two maps'
  corners do not line up (c1a3a's and c1a3b's; `liftCornerAhead`).
  A way out whose landmark has trigger_transition volumes goes only while
  the player is in one of them (`LogicKind::Transition`): c2a5c's door that
  shuts behind the arriving player fires the way back, and walking on is
  what keeps them in the map.
- A track train the map lets the player drive (all but those it marks
  NOCONTROL, and those whose track has taken them over) is driven as in the
  game: USE, standing on it -- anywhere in it, or, where the map has a
  `func_traincontrols` (c2a2g's and c2a3's crash train), in that box, cooked
  into the train's record in its own space -- takes the controls, and a
  press of forward or back steps its speed a quarter at a time, through
  stopped to backwards (never backwards for one marked FORWARDONLY). Backing
  up, it runs back along its track, facing as it faced, and a node it backs
  past fires again. USE, a jump, a step aside or getting off lets go; the
  train goes on as it was going. The display shows the game's picture of how
  it goes (back, stopped, three speeds), and the controls are kept across a
  level change and in a save (`trainUse`, `trainControl`,
  `shared/HlLogic.hpp`). Track trains have the game's engine sound, higher
  the faster they go, and brakes.
- Fans and doors that turn about any axis; boxes that break (with USE, until
  there is a crowbar) and boxes that are pushed; volumes that hurt, push and
  teleport; health, and dying.
- How hard the game is: NEW GAME asks, as the game's does -- EASY, MEDIUM
  or DIFFICULT -- and every number of the game's `skill.cfg` is taken at
  that level (`shared/HlSkill.hpp`, the three columns typed in and checked
  against the file by `test_skill`): the monsters' health and blows, the
  rounds of the soldiers, turrets and tanks, what a health kit, a battery, a
  scientist and a wall charger give. The level is kept with the game's
  switches (`kGlobalSkill`), so it goes into the next map and into a save; a
  save from before there was a choice is MEDIUM, as everything was. A game
  between consoles is played at the host's (its START says which). The
  SDK's own rules by level are kept too: on DIFFICULT a grunt of a squad
  tosses a grenade as he runs for cover and fires his launcher again
  sooner, an assassin standing still fades almost out of sight, an alien
  slave's zap goes half again as fast; on EASY the Apache does not fire its
  rockets with its gun. And autoaim (the OPTIONS' CONTROLS, AUTOAIM, off
  unless asked for: the game's `sv_aim`): on EASY and MEDIUM the pistol, the python, the MP5 and
  the shotgun bend their aim to the monster nearest it within their cone
  (10 or 5 degrees) -- a third of the way on EASY, nine tenths on MEDIUM, as
  the game's GetAutoaimVector has it -- and the crosshair moves with it (red
  while it is on one, on EASY); on DIFFICULT the aim is straight.
- Wall chargers are used for as long as USE is held on them (its button, the
  touch screen's USE), as the game's are: their first sound, then their
  loop while they give, a point a tenth of a second, stopped a quarter of a
  second after USE is let go or as soon as the player is full (to someone
  already full they are silent); empty, or to someone without the suit,
  they refuse, with their own sound. What they
  hold is the level's.
- What crushes: a door or a train with a `dmg` hurts the player who holds it
  up (a door each time it tries, a train every half second, as the game's
  `Blocked` do), and a monster it comes into while it moves -- it does not
  push monsters, so it goes through them instead: c1a1's rising wreckage
  kills the scientists over it. A `multi_manager` taken away by a killtarget
  fires nothing more of what it had still to fire. A `func_pendulum` swings
  as the game's does (pulled to the middle of its distance, never faster
  than its speed), dies down with `damp` until it rests in its middle, and,
  stopped with flag 16, goes back to where it began. An explosion reaches a
  brush as far as where the line to its middle meets it, and only if that
  line gets there: a long pipe is hurt near either end, a box behind a wall
  is not.
- Xen pulls less: a `trigger_gravity` sets how hard the world pulls the
  player who touches it (0.6 of it through most of Xen, 0.3 in the
  Nihilanth's lair), from then on, until another sets it again. It is kept
  with the game's switches (`kGlobalPlayerGravity`), so it goes into the
  next map and into a save. A `trigger_push` is what the game makes of it,
  the player's base velocity: upwards an acceleration, along the ground a
  carry, in the water both (`PlayerState::push`, `shared/HlPlayerMove.hpp`).
  A jump onto one of Xen's pads throws the player high, and they come down
  on it as fast as they went up: unhurt.
- Slippery ground: a `func_friction` is drawn as its render mode says (the
  faint sheets of spilt water in c1a1 and c2a1) and walked through, and
  while the player stands in it the ground holds them only its `modifier`
  in percent of the world's friction (`PlayerState::slip`, set each frame
  as the game's move puts its own back after each).
- Xen's plants (`shared/HlXen.hpp`, `ActorKind::Xen`): the hair and the
  three fungi sway, each at a pace of its own, and the fungi are in the way
  as the game's boxes have them (the big one's cap up on its legs). A light
  on its stalk draws in and goes dark as the player comes near, switching
  off the light it casts, and comes up again five seconds after; its glow is
  drawn at the top of its stalk. A tree strikes at what comes in front of
  it: 25, and a shove.
- Level version 39's: `trigger_counter`, `trigger_changetarget` (a record's
  target, changed, is saved with it: `LogicState::target`),
  `player_weaponstrip` (all but the suit), `player_loadsaved` (the screen
  goes dark, its words, then the last save), `trigger_endsection` (back to
  the title: the end of the training), `trigger_monsterjump` (what fights
  is thrown up as it comes in), `trigger_camera` (the view from somewhere
  else, looking at what it names; the display and the weapon in hand are
  not drawn meanwhile, and with its flag 4 the player cannot move),
  `speaker` (the announcements of its group, now and then), `func_guntarget`
  (rides its path; shot enough, stops and fires its message),
  `button_target`, `env_blood`, `cycler_sprite`; and `item_antidote`,
  `item_security` (counted, as the game counts them) and `item_airtank`
  (air, again half a minute on). A map's cook report names what it does not
  handle yet ("not handled:").

- A box that is pushed collides as the size it is. The map's collision
  knows only shapes a person wide, so a wider box is that shape at each of
  its corners (and between), and goes as far as the first of them to meet
  something; where it comes to rest is asked of the map's collision every
  eight units across it; against other boxes it is box against box. The
  player and a box are never left in each other: a box that finds the
  player inside it gets out of them by the shortest way, and if it cannot,
  they are moved out of it. What moves -- a door, a lift, a train -- pushes
  a box in its way, or is stopped by it as it is by the player, and carries
  one that stands on it. Its scrape along the floor is the game's.
  (`shared/HlLogic.hpp`, `pushableTick`; `test_boxes` shoves every box of
  four of the game's maps every way and looks.) Holding USE on one stops
  the push and pulls it instead: it takes on the player's speed a quarter a
  frame at 60 frames a second. USE held on the ground cuts the player's top
  speed to a third, with or without a box, as the game does. Not in a match, where a
  guest's USE isn't sent while held. In water a box floats as the game's
  do: gravity and a lift as deep as it is in, times its buoyancy by its area,
  its bobbing dying away in about half a second; a swimmer pushes it at a
  tenth. A box meets a brush that is turned (a door on a hinge) with one
  shape at its middle, as the game does with one hull: by its corners, a lid
  stood open would catch c2a3's barrels well clear of it.
- The long jump module (`item_longjump`), for someone in the suit: running,
  with the crouch pressed under a second ago, a jump goes 560 units a second
  along the view, about 450 on the flat with the legs tucked.
- Water: swimming, sinking, jumping out onto a ledge, drowning, slime and
  lava; liquid surfaces sway. The sky, as a box of six pictures behind the
  world: the game's `desert` for a map with open sky that names none
  (crossfire), as the game gives it.
  Water need not stand still, nor be a `func_water`: anything made of
  brushes that can be walked through and whose `skin` says water, slime or
  lava is that, wherever it is -- a map floods a tunnel by sliding a train
  of water up it (c1a1b's valve; Lambda Core's coolant). A valve's wheel
  (`func_rot_button`) is a button that turns: used, it turns its distance,
  fires what it works when it is round, waits, and turns back. A current
  (`trigger_push`) may start switched off and be switched on by whatever
  targets it. Still missing: the wheels that are HELD (`momentary_rot_button`,
  `momentary_door`: nineteen, in eight maps).
- Saving, as the game has it: AUTOSAVE, which the game writes itself at a
  map's own save points and on arriving in a map (the OPTIONS' OTHER, AUTO
  SAVE, turns it off; the hidden save a death falls back to is still made).
  On arriving it is written behind the frozen picture, before the sound
  starts; at a save point a piece a frame, the game and the music going on.
  It only names the maps left, in the run's folder, rather than copying
  them, so it is made again whenever that folder changes. QUICK, the pause menu's
  one-touch save; and **as many of the player's own as they make**, up to
  99 (`save1.sav` ... `save99.sav`: the six slots there used to be are the
  first six), each made new or put in another's place, and each deletable.
  The index beside them (`index.dat`, 'HLS2') holds only the saves there
  are; one from before (the nine slots of 'HLSI') is made again from the
  files, which the console lists with `opendir` rather than asking for a
  hundred names. A save holds **the whole of the map** it was made in --
  every door, lift, train and trigger as it was, every actor where it
  stood and what it was doing, who came into the map with the player,
  what was in flight, the marks on the walls -- so loading goes on from
  that moment, not from the map's beginning (15 to 60 KB a save). At
  start there is a **title**: CONTINUE (the most recent save), LOAD GAME
  (the list, each line with how long was played, the map, the day and
  time by the console's clock, and the chapter -- which the converter
  works out for every map from the game's own: each chapter is named by
  the map that opens it), NEW GAME (which touches none of the player's
  saves: HALF-LIFE, the HAZARD COURSE or UPLINK, as the game's menu offers
  them -- each only if its first map is on the card -- then how hard), OPTIONS.
  A write is said on the screen (SAVED, AUTOSAVED, or that the card would
  not take it) and is safe against the console being switched off in the
  middle of it: the new file takes the old one's place only when it is
  whole, and what is found half-way at the next start is put right.
  How it is made (`shared/HlSaveFiles.hpp`, `shared/HlSaveState.hpp`):
  the state is written with yas a field at a time, never as memory lies;
  no list of fields is kept by hand -- each structure is taken apart
  whole, and one that has gained a field does not build until its count
  is corrected (`shared/HlFields.hpp`), so a new field cannot be left out
  of a save by forgetting; and nothing is read that is not shown first to
  be this build's own writing of this cooked map (a fingerprint of the
  layout, the map's checksum and counts, the exact length, under the
  file's own checksum). The test that guards it (`test_savestate`) plays a
  real map for a while with things made to happen, saves, lays the save
  over the same map begun afresh, plays both on, and requires them equal
  to the byte: all 95 maps of the campaign pass it. **The maps left on
  the way are kept as they were left** (`shared/HlMapStore.hpp`): leaving
  one by a way out writes its whole state -- the same as a save's, and its
  monsters' minds -- to a file of its own in a folder of the run beside the
  saves (`saves/run/<map>.mst`, 30 to 50 KB), a piece at a time (a DS has
  no room for it in memory); coming back by a way in lays it back over the
  map begun afresh: the door opened is open, the monster killed lies where
  it fell, the charger is as drained as it was, who went along with the
  player is not there twice. Twelve maps and 640 KB are kept, the oldest
  left let go first; a map cooked again since, or let go, is come back to
  with only what is broken, killed and picked up there (the saves'
  per-map bits). A save holds them too, copied in after the rest (save
  version 9; the one made coming into a map only names the folder), and
  loading one puts the folder back as it was. What a save does not hold:
  a sound half played and an effect on the screen start again (the music
  goes on from where it was);
  a save and a cooked map each say what format they were written in and the
  oldest build that can read them, and a build knows the newest format it
  writes and the oldest it reads (`HlLevel.hpp`, VERSIONING; `HlSave.hpp`):
  a change that only adds to a map's data, or that no save points at, does not
  stop the saves already made from loading. One outside that range is listed as
  "from another version", not loaded; the saves of the game's earlier builds
  (`slot0.sav`, `slot1.sav`) are not read.
- Models: the game's studio models, cooked to a DS format (one skin texture a
  model, animation as keys) and skinned by the hardware's matrix stack, at
  several levels of detail (below).
  Scientists, guards and props stand and animate in the maps; scripted
  sequences play (an actor goes to its mark as soon as a script takes it,
  waits there, acts when told, and what waits on it happens when it is
  done); actors cross level changes with the player. The crowbar is in hand.
- Monsters: headcrabs run at the player and leap, zombies walk up and swing,
  barnacles hang with their tongues down and pull up what walks into them.
  They move by the game's own rule for things that walk (a step is made if,
  lifted a stair's height, nothing is in the way and there is floor where it
  ends), straight at the player where that can be walked and otherwise from
  path node to path node: the converter joins the map's nodes up and works
  out the way from each to each, so the console only looks it up.
  Monstermakers make them.
- Weapons: the crowbar, the pistol and the shotgun, found where the game
  leaves them, with its numbers at the middle difficulty (a zombie shrugs off
  most of a bullet, as it does there). Ammunition, health kits, batteries and
  the wall chargers; armour. What is killed or picked up stays so when a map
  is come back to, and in the save.
- Sound: the game's own, converted to the 4-bit ADPCM the DS's sound chip
  plays by itself. Weapons, monsters, doors, buttons, breakage, footsteps and
  pick-ups; the maps' ambient sounds, louder and to one side as the player
  nears them; the announcer and the scripted lines of scientists and guards
  (from the game's sentences.txt), read off the card as they are spoken; and
  the music, decoded from the game's MP3s and streamed from the card. A map's
  sounds have an allowance of memory (350 KB); the longest are cooked at half
  the rate where a map has more than that.
- On a DSi the sound has more to it. SOUND: Quality high makes all of it at
  47.6 kHz (the ARM7's pipeline above). Room effects: enhanced gives the
  rooms to the DSi's DSP instead of the ARM7: the ARM7 sends what the
  sixteen channels play, in one channel at 11.9 kHz, the ARM9 passes it on
  through the DSP's port (never the DSP's own bus, which jams against the
  display lists' DMA: tools/hwtest/dsp_bus), and the DSP puts out the room
  alone, in stereo, a quarter of the speakers to it and three to the
  ordinary sound (made up for by the ARM7). Its room is richer than the
  game's: the first reflections at four places back along a line the
  room's size long, two to each side; the reverb, four lines feeding each
  other through a Hadamard square, each dulled on the way round, two to
  each side; and the game's echo, up to 0.4 s off; each room's numbers
  worked out from the game's own (`shared/HlDspRoom.hpp`) and written in
  when the player walks into it. The music is the ARM7's either way.
  The DSP program is in
  `teak/`, and is assembly, all of it: compiled C runs on this chip no
  faster than on the ARM9, and what it is good at -- multiply and add in
  one step, a loop that costs nothing to go round -- a compiler does not
  use. What the program must work out is written in C once, in one header
  (`shared/dsp/hl_dsp.h`, its model, which the host tests run), and
  `tools/dspsim` runs the real program in an emulated DSP and holds every
  pair of samples that comes out against the model's: a quiet scene, and
  one with everything as loud as it goes. By the same tool the room costs
  the DSP 5.3% of its time.
  The DSP is built not to make a noise when something goes wrong: its
  interrupt only hands finished samples to the speakers (the working out is
  in its main loop, and a port left short gets the last sample, fading); a
  room's lengths are each held within their line whatever it is sent; and a
  DSP that stops answering is given up for the session (the game's own
  rooms then), said in `perf.txt`.
  melonDS (1.1) is no judge of it: it runs the DSP only when the game
  touches one of its registers.
- More to fight: alien slaves zap from a distance and claw up close,
  bullsquids spit, bite and whip, houndeyes run up and let off a blast. Hand
  grenades bounce, and burst after three seconds. Bolts, spit and blasts are
  drawn as plain shapes.
- Turrets, by the game's own rules: the miniturret (on a floor or hung
  from a ceiling), the sentry and the big turret. Folded away until one sees
  someone or is switched on by name (a door, a tripwire's relay); out,
  sweeping and pinging; onto whoever it sees, turning as the game's does and
  firing ten rounds a second with no spread; away again when nobody has
  come. A sentry is woken by a touch or a shot and wakes the others of its
  name; plating turns a bullet; one nearly done for goes wild. The big one
  has to spin up. Their guns are aimed by the models' own bone controllers,
  which the converter now keeps (the barnacle's tongue is one too).
- Round 4 of the monsters, each from its file in the game's code
  (`shared/HlMilitary.hpp`, `shared/HlAliens.hpp`; `test_military` plays each
  on its own map). The osprey flies its path of corners on the game's
  spline, its rotors tipping and heard from far off, and at a corner of
  speed 0 hovers and lets soldiers down on ropes in the places of those of
  the map's who have died; shot down (only a blast, or a hit to the cockpit
  or an engine, counts), it falls burning and goes up. The gargantua is hurt
  only by what burns, crushes or blows up: it swipes up close, sweeps its
  flames a little further off, and from afar stomps, a shock that runs along
  the ground faster and faster; it dies in blasts, and is gone in pieces.
  The ichthyosaur swims its water by the game's steering, circles what comes
  into it, nearer and quicker while it is not looked at, charges, bites, and
  floats belly up when it dies; it is drawn nosed up or down and rolled. The
  assassin leaps, shoots as it falls, throws a grenade once it is frustrated
  and runs for cover (it is cloaked only at the game's hard skill, which
  this port is not). The baby crab is a smaller, weaker headcrab that leaps
  from closer; the dead in their suits lie as the game poses them.
- Round 5, the rest of them (`test_bosses`, `test_tanks`). The tentacle
  rises out of its pit to the height of what it hears and strikes there,
  finding its way between its many clips by the model's own graph of them;
  shooting only sends it back down, and the map puts it down. The apache
  flies by its rotor's lift and its tipping, as the game's does, along its
  corners and at the player, with its gun and its rockets in pairs, drawn
  pitched and banked. Big momma walks her path of nodes, given new health at
  each (hurt to nothing she goes on to the next), slashes, spits acid from
  afar and lays baby crabs. The nihilanth floats between the map's heights
  with its twenty spheres about it, which heal it, and goes back to its
  crystals for more; it zaps, throws the ball that takes the player away to
  the map's next arena, calls up slaves and controllers, and is hurt to its
  death only in its head once its crystals are gone and it has opened. The
  flocks of flyers keep together behind a leader that steers off the walls.
  A soldier told to comes down a rope (monster_grunt_repel), as the
  osprey's do.
- Guns on mounts (func_tank, and its laser, rocket and mortar kinds): they
  turn to the player within their reach and at their rates, and fire on
  target for as long as they have lately seen them. At a func_tankcontrols,
  USE takes a gun: it follows the view, fires at the fire button (the
  player's own weapon put away, the crosshair kept), and is let go of by USE
  or by walking off. Firing, a gun shows the map's own flash and smoke
  sprites at the map's scale (the smoke rising), and none if it has none.
  Mortar fields drop their shells on whoever calls them,
  at random, or where two wheels (momentary_rot_button, which turn from one
  USE to the next, and the momentary_door bars they move) say.
- A hit to the head counts three times, as in the game. Where a shot lands
  is found on the model's own hit boxes, posed as the monster stands; a shot
  that goes through its outline but between the boxes still counts, as a
  hit to the body.
- The bullsquid is as wide as the game has it: the converter cooks the
  world's third clip hull, and it walks only where that has room.
- A monster the map marks as a prisoner goes for nobody (what is glimpsed
  during the cascade), and a map's "kill" of a name takes people and
  monsters away as well as brushes.
- The game's sprites. The converter reads its `.spr` files and cooks each
  to one small texture, its frames side by side (`spr/` on the card;
  `shared/HlSprite.hpp`). Most of them are "additive" in the game -- light,
  added to what is behind -- which the DS cannot do: how bright a texel was
  becomes how solid it is. Against the dark that is the same; against
  something bright it is dimmer than the game's.
- What the maps show and shake, from their own entities
  (`shared/HlEffects.hpp`): sprites and glows (`env_sprite`, `env_glow`; a
  glow, render mode 3, by the engine's rule (`glowFalloff`, HlSprite.hpp):
  unless its render fx is 14 it is drawn dist/200 of its size, the same on
  the screen near or far, and only 19000/dist² as bright, from 0.05 up.
  So a ceiling lamp far off is a bright point, as in the game, not a big
  disc), beams and lasers (`env_beam`, `env_laser`: straight or thrown about like
  lightning, striking and striking again as the map times them; one that
  burns stops on the first thing in its way -- the player, a monster,
  something that breaks -- and burns it, so c1a1's lasers cut through its
  computers and set off what they fire, with sparks where the map asks for
  them), explosions (`env_explosion`: seen,
  heard, felt and hurting), shakes of the view (`env_shake`), sparks
  (`env_spark`), things thrown out (`env_shooter`, `gibshooter`),
  `env_render` making a brush see-through or unseen, and fades of the
  screen (`env_fade`): in their colour and timed as the game's screen fade
  is. A fade from a colour lifts from its first moment, and its hold time
  only matters to a fade into one. Black and white are the hardware's
  brightness. Any other colour is BG1, one colour out of Bank D, blended over
  the 3D picture and the sprites (which sit at priority 1 for it, so the HUD
  is under it too). The hardware's 16 steps of either are smoothed by
  showing the step below and the one above in turn, frame by frame.
- The port's own effects are drawn with the game's pictures: a grenade's
  fireball, an alien slave's lightning, a turret's tracer and muzzle flash,
  a bullsquid's spit, a houndeye's shock wave; a flash at the player's own
  gun, a puff where a shot lands on something that bleeds; the view shakes
  at an explosion near by.
- The HEV suit is taken where the game has it (it is what opens the way on),
  and armour is for someone wearing it.
- The seated and the dead are let down onto what is under them, as the game
  lets them down (a scientist at a desk, and an item, in the low box the game
  drops them in, onto what is under them whether it is the world or a brush --
  a chair is often a func_wall, and c1a0d's suit stands in a case too low for
  the player's box; a body, and furniture, as a point); a scientist at a desk
  goes between his five ways of sitting.
- USE takes the first of what is faced as squarely, as the game does: c1a0d's
  microwave is five buttons in one box, each locked until the one before it
  is pressed.
- A `+a` texture has its `+0` pictures as its other face, as a `+0` has its
  `+a` ones, and a brush unseen until an `env_render` shows it is cooked,
  unseen.
- People sent somewhere by a script walk there as the monsters walk: round
  what is in the way, by the map's path nodes. Only those the landmark can
  see cross a level change, and only if there is room for them on the other
  side.
- The renderer listens to the hardware: it reads how many polygons and
  vertices a frame really used, whether any were dropped, and how close the
  rasteriser came to not finishing its lines, and moves its budget by that --
  up, where the hardware throws away what is off the screen (often a fifth of
  what was counted), and down when a frame was too much. The same numbers are
  on the debug overlay and, for each map played, in `perf.txt` on the card --
  with the debug readout on, or in a build with `HLDS_PROFILE`: otherwise
  nothing is written there (a line after every map rewrote the directory of
  the folder the maps are in, the write a failing card can take them with).
- Player movement with Half-Life's own numbers: walking, jumping, crouching,
  steps, climbing, sliding along walls, in fixed point throughout (the DS has
  no FPU).
- The arithmetic. Everything that runs every frame is compiled as ARM
  (the build tool had been compiling the `.arm.cpp` files as Thumb, where
  every 64-bit multiply is a call), and the compiler's 64-bit divide and
  the fixed-point square root go to the console's own divider and
  square-root unit (`source/HwMath.s`): same answers, 54 bus ticks a divide
  and 28 a root on a DSi (`tools/hwtest/divmath/`). The link wraps the
  library's two names (`--wrap`), and what the unit would answer otherwise
  still goes to the library: a divide by zero or by -1, and unsigned numbers
  with the top bit set. `tools/hwtest/divcontract/` holds quotients and
  remainders against the library's, word for word.
  A build with `HLDS_PROFILE` checks the hardware's sums against the slow
  way at start-up and says so in `perf.txt`.
  A 64-bit divide by a constant whose numerator fits 32 bits is done in 32
  bits, where the compiler makes it a multiply (the fine sines had eight
  trips to the divider each). Products of two 16-bit values -- a bone's
  rotation, a speed times the time step -- use the ARM9's own 16-bit
  multiplies, which the compiler never picks: `shared/HlDsp.hpp`, checked
  against their definition by `test_dsp` and on the console by
  `tools/hwtest/dspmath/`.
- Where the code runs from. The DS has 32 KB of memory that is always as
  fast as the cache at its best (ITCM), and what is in it is chosen by how
  much of the frame it is: the hull trace -- for everything that traces, not
  only the player --, the choosing and sending of the world's surfaces, the
  posing and drawing of the models, a walking monster's step, and the sines
  and lengths they all use. `HL_HOT` (shared code) and `HL_ITCM` put a
  function there; `HL_ARM` builds one function of a Thumb file as ARM (the
  effects, the head-up display and the other things drawn every frame had
  been Thumb, each product a call). 22 KB of the 32 are used; the fast
  *data* memory (DTCM) is left to the stack, which was measured 11 KB deep
  of its 16.
- A trace no longer asks every brush of the map whether the move comes near
  it. A map has some hundreds, each a hundred bytes of record: now there is
  a table of twelve bytes a brush, read first (`MoverReach`). At the fight
  in c1a2 that took a quarter off the logic's time.
- `tools/arm9-report.sh` says what the built program is made of: what is in
  the fast memory byte by byte, which of the game's functions still call the
  library's arithmetic, which are Thumb; `--check` fails if one of the
  frame's inner loops has come out of fast memory or calls a routine for a
  multiply. `./make.sh` prints its first line.
- On a DSi the speakers' rate (32.7 or 47.6 kHz) and their share between
  the console's sound and the DSP are asked of libnds
  (`Arm7Audio::setDsiRate`, `setDsiRatio`), both sent every time:
  `soundEnable()` puts them back behind the game's back.
- Soldiers (`shared/HlGrunt.hpp`): the game's human grunt by its rules and
  its numbers. Cooked with the head, gun and skin of what the map has him
  carry (MP5, shotgun, hand grenades, the launcher). He sees what is in
  front of him within 2048 units, and turns to a shot or a blast; fires
  bursts of three rounds a tenth of a second apart, four bursts running
  (a shotgun: a shell of five pellets), standing or crouched, and never
  with one of his own in the line; reloads when the clip is out, behind
  cover if there is any; kicks whoever is within reach; throws a grenade in
  the game's arc, or launches one at where the player will be -- never from
  within 256 units, never onto his own, never twice in six seconds, and
  first of all when the player is out of sight; runs from a grenade that
  lands by him; takes cover nine times in ten when he is hurt, and limps
  under twenty health. His helmet takes twenty off a round, a round in the
  head counts three times, and he dies as he was hit.
  Squads are the game's: by the map's names where it names them and flags
  a leader, else whoever of his kind a soldier can see within 1024 units
  when the map starts. The whole squad learns of the player at once; the
  leader calls it, waves the others on the first time, and opens fire while
  they make for cover; two of a squad fire at once and two may have a
  grenade in hand; the rest hide, or go in for a line of fire. Cover is the
  nearest place the player has no line to: a few steps to one side, or one
  of the map's path nodes. They talk -- the game's own lines for a grenade,
  the alert, cover, a charge, a taunt, and the questions and answers of
  soldiers with nothing to do -- off the card, one line at a time.
  Monstermakers make them, scripts move them, and what a map has one set
  off when he sees the player, is hurt or dies is set off.
- Tripmines: armed two and a half seconds after the map starts (one, where
  the map says), a beam to the first thing in its way; the mine goes off
  when the beam gets shorter or longer by anything -- the player, a
  monster, a door -- or when it is shot or caught in a blast: 150, to 375
  units, so that one sets off the next.
- The MP5 and the .357, found where the game leaves them. The MP5 fires ten
  rounds a second from a clip of fifty (9 mm, which it shares with the
  pistol), and **R** launches a grenade that goes off at what it touches;
  the .357 is six rounds of forty.
- The guns and the hand grenade behave as the game's do, from its own
  weapon source (`helpSrc/`) and its view models: **L** is the trigger and
  **R** the game's second one. The pistol fires every 0.3 s, and every
  0.2 s and ten times as wild on R; its last round and its two reloads
  (with a round still in it, and without) have the hand's own clips. The
  shotgun is pumped half a second after it fires, which is when the spent
  case comes out; R fires both barrels (two shells, twelve pellets, a
  second and a half); it is loaded a shell at a time, half a second each,
  and the trigger breaks that off. The MP5 and the .357 have each their
  two shots at the game's pitches. A hand grenade has its pin pulled by
  the trigger and is thrown when the trigger is let go -- harder the
  higher it is aimed, with one of three throws -- and goes off three
  seconds after the PIN, in the hand if it is still there. Every gun but
  the pistol only clicks under water; an empty one clicks once for a pull
  of the trigger. Rounds go off line by the game's cone (most of them near
  the middle), not anywhere in a square. Left alone, the hand does what
  the game's does, by its odds. A reload's sounds are in the hand's own
  animation, as the game has them -- the magazine out, and in -- and are
  played from there. All of it is in `shared/HlMonsters.hpp`
  (`weaponsTick`), run by the host tests. Reloading is the touch screen's
  RELOAD, a touch on the weapon in hand, or pulling the trigger on an
  empty gun; R reloads only the .357, which has no second trigger.
- What is taken far past dead comes apart: into the game's own pieces (the
  skull and four more for a person, four for an alien), which fly, tumble,
  bounce, lie, and are gone after a while. A body caught in a blast does
  too. What the maps throw out (`env_shooter`) is the model the map names.
- The big turret's eye glows red at each ping, where the model has it.
- Beams whose ends are things that move follow them (the lasers on the
  trains of c1a1, the cascade's); a beam with one end left to chance
  strikes somewhere new each time, as far as the world lets it; one with
  several targets of a name picks among them; rings are rings.
- Everything see-through -- glass, sprites, beams, effects -- is drawn in
  one order, far to near, and what the effects cost is counted against the
  polygon budget with the rest.
- Sound in layers, played by the game's own ARM7 program (`arm7/source`,
  in place of BlocksDS's default with maxmod; it keeps the rest of what that
  did: input, wifi for Download Play, the DSi's card, the clock). The ARM9
  (`source/audio/Arm7Audio`) puts commands -- play this clip, change, stop,
  the music's ring -- into one block of shared memory
  (`shared/HlAudioLink.hpp`); the ARM7 takes them every 2 ms block, plays
  each sound on a free hardware channel straight out of the map's cooked
  sounds in memory (the hardware decodes the ADPCM, loops included), and
  says when each has ended, by the token it was given: a voice is free the
  moment its sound is over, and a spoken line's next word is queued behind
  the word being said, on its channel, to begin in the same 2 ms block
  that word ends (no gap between words; a word the line cuts short is
  still cut by its time). 66 KB of code and 19 KB of data on the ARM7,
  against 110 KB for the default with maxmod; the ARM9 links no maxmod
  either. Every sound is of
  a layer -- the player's own, the world's, the voice, the loops of a
  place, the menu -- and each layer has its share of the fourteen voices
  the hardware really has beside the music (3, 4, 2, 4, 1), which no other
  layer can take from it; over its share a layer has only what is free,
  and gives it back, the quietest first, to whoever is short
  (`shared/HlMixer.hpp`). If the hardware refuses a sound anyway, what
  matters least gives its channel up and the sound is asked for again:
  nothing is counted as played that was not. So a far headcrab does not
  cut off the player's gun.
  **Two people may speak at once**: a line cuts in only on the same
  speaker's last (the same place -- or no place, which is the voice heard
  everywhere), and the next word of a line is read from the card while the
  one before it is said. A scripted line comes from where its speaker
  stands. What has a place goes on being placed as the view moves and
  turns. Sounds are played at the **pitch** the map asks for; up to eight
  looping ambiences run at once where voices are free; what moves (a lift,
  a train, a door, a fan) is heard for as long as it moves, if its sound is
  one that goes round, and a sound whose file has no loop point is played
  once, as the game plays it. Buttons have the whole of the game's list of
  sounds, and a door or button that will not work answers with its locked
  sound and line -- and, the day it works for them, with its unlocked
  one. A line is said word by word as the game's sentences.txt marks it:
  each word at its own pitch and loudness, cut short where it says, with
  the pauses of its commas and full stops. A voice that gives way to
  another sound is faded over a few frames, not cut (a hardware channel
  is kept back for that: thirteen voices and one on its way out). The
  player is heard on ladders, in water and landing, and a step sounds of
  what it lands on -- metal, dirt, a vent, a grate, tiles -- by the game's
  own list of what its textures are made of (`sound/materials.txt`): the
  converter marks each texture, and the console finds the floor under the
  feet once a step (`shared/HlMaterial.hpp`). What the models themselves
  sound like as they move -- a footstep in a walk, a keypad in a scene --
  is in their animations, and is played from where they are, at full
  volume and heard only near, as the game's ATTN_IDLE has it. A body
  hitting the floor is in them too (the game's events 2001 and 2002):
  bodydrop3 or 4, the heavy one at pitch 90, only if there is floor under
  it. A fall that kills, and a snark's burst, add the game's bodysplat.
  **In the pause menu everything stops where it is, music included, and
  goes on from there** (a DS's channel cannot be stopped and continued: it
  is slowed to the slowest the hardware has and silenced, which costs a
  sound a quarter of a second for ten of menu); the menu's clicks are
  heard meanwhile. The music is the track's stereo file (`.hsd`, 23.8 kHz
  IMA, on a DS and a DSi alike: the converter writes nothing else), its
  blocks put into a ring by the ARM9 as they are
  read off the card, decoded on the ARM7 and drawn out to the output rate
  (`shared/HlStereoMusic.hpp`), as loud as the music slider says.
- The pipeline (`arm7/source/Pipeline.arm.cpp`): the console's own mixer
  adds up the sounds' channels; its left and right are captured into rings
  in the ARM7's memory, and every 64 samples what was captured, with the
  music added, is written 7.8 ms ahead into the rings channels 1 and 3 play
  -- which the speakers take directly and the mixer, so the capture, does
  not hear: what is added is never captured again (tools/hwtest/routing
  measured it on a DS and a DSi). The room's effect is put on what was
  captured there (`shared/HlRoomDsp.hpp`): the game's 29 rooms
  (`shared/HlRoomPresets.hpp`, its own table of numbers), the lowpass under
  water, the wandering of weirdo 1 and the left side's lateness at the
  output rate, the reverb and the echo at about 11 kHz, as the game's run;
  a room changed fades out and in. The room is the nearest env_sound the
  player is within, looked at four times a second (under water, 14); the
  music is added after it, as in the game; then the sound is laid out for
  the console's speakers (a little narrower), headphones, or one channel
  (SOUND: Room effects, Output). About a fifth of the ARM7's time. On a
  DSi, Quality: high makes all of it at 47,605 Hz, the rate its speakers
  are fed at (standard: 32,728, and the speakers fed at 32.73 kHz). While a
  save is written the pipeline steps aside (the plain mixer): on a DSi the
  ARM7 does the card's writing and is held up by it; and held up longer
  than the rings are ahead for any other reason, the pipeline empties them
  and goes on from where the channels are -- silence, not the rings played
  round again.
  With the DSi's DSP option on, the DSP has the capture, and the pipeline
  is off.
- The pause menu: RESUME, QUICK SAVE, SAVE GAME, LOAD GAME, NEW GAME (asks
  first), OPTIONS, ACHIEVEMENTS, COMMAND. Buttons act when the stylus is
  lifted from them. The command line has a keyboard of the port's own.
- OPTIONS, the saves and the achievements are **sheets** (`Menu::sheetFrame`,
  `source/ui/Menu.cpp`): a page at a time between < > (OPTIONS: SOUND,
  GRAPHICS, CONTROLS, TOUCH AIM, BUTTONS, OTHER), its rows to touch in a
  list that is slid aside for the next page (or L/R) and dragged up and
  down when it is longer than the room, a box that reads out the row
  touched (what a setting does; a save's chapter, map, health, date and
  play time), and buttons along the bottom that ask "...? YES NO" there.
  - A setting's row changes it when touched (left and right on the pad
    too); a slider follows the stylus on its track. BUTTONS: touch an
    action, then press its button. RESET puts the page shown back as it
    was at first (`Settings::reset`).
  - The saves, newest first: NEW SAVE at the top when saving (none past
    99), a save touched twice (or SAVE, LOAD) to use it -- one put in
    another's place, or loaded in the middle of a game, asks first --
    and DELETE for the player's own.
- The touch screen is laid out as the DS's better first-person games lay
  theirs out (Dementium II is the model), in the suit's amber on dark: a
  bar along the top with MENU, USE in the middle and RELOAD (faint when
  there is nothing to reload; health and the suit's charge are on the top
  screen and not said twice); a framed aim pad; JUMP, the lamp and DUCK in a column beside it; and along the
  bottom a slot for each weapon, always in the same place, with the
  game's own picture of it (cooked from your `sprites/320hud1.spr`), the
  one in hand lit with its rounds under it. A touch on a slot slides up
  the list of that slot's weapons over the pad, as the game's numbered
  menu lists them (`shared/HlWeaponList.hpp`): each its picture and name,
  the one in hand lit, one with nothing to fire grey; a weapon in it
  touched -- or the slot pressed and the stylus slid up onto one and
  lifted -- is taken; a touch elsewhere, the slot again or three seconds
  of nothing puts it away. A slot with one weapon takes it at once. It
  rises out of the pad's bottom edge and stays inside its frame, clear of
  its cut corners, so the pad's own drawing covers everything it leaves; it
  is dropped when the menu opens or a map loads. As it slides, the pad and
  it are drawn off the screen and put on it in the next vertical blank
  (`BottomScreen::present`): never seen half drawn. The top screen's
  display is drawn the same way (`HudDraw::present`), so numbers that
  change (health at a charger) do not flicker. It sounds as the game's menu does
  (`cl_dll/ammo.cpp`): wpn_hudon up, wpn_moveselect from one weapon to
  the next, wpn_select for the one taken (the player's own choice only --
  not when the game puts a spent weapon away), wpn_hudoff away with
  nothing taken. USE says whether it found something: wpn_select, or
  wpn_denyselect, at 0.4 as the game has them.
  DUCK on the screen is held to crouch and can be slid off onto the pad
  to look about still crouched (lifted, the player stands); with the
  crouch a toggle it is tapped once to crouch and once to stand, which
  leaves the stylus free to aim and to touch USE. WALK (the game's +speed,
  three tenths of the speed) works the same way, and can take the jump
  button's place, since two taps jump anyway. As in the game a walk is quiet,
  not silent: footsteps go by time and speed, measured against the real game
  (a run 0.5 each 300 ms, a walk 0.2 each 400 ms, crouched 0.35 of that each
  100 ms later). One table (`shared/HlTouchLayout.hpp`) says where everything
  is, to the code that draws and the code that tests a touch alike. For
  a stylus in the left hand, "Buttons: left" in the options mirrors it.
- The weapon in hand is never cut by a wall: the game gives it a depth
  range of its own, which the DS cannot; so it is drawn a quarter of its
  size about the eye -- the same picture, a quarter as near, in front of
  any wall the player can stand against -- through a projection whose near
  plane is a quarter as near too (`ModelDraw::drawHeld`).
- Light that changes. The world's light is baked into its vertices, but
  not all of the game's light is steady: lights a switch turns on and off
  (its "styles" 32 and up) and lights that flicker, pulse or strobe (1 to
  12, the game's own ten-a-second patterns). Where such light reaches a
  face, the converter gives every vertex of it a colour word of its own
  and records what the vertex is without that light and what each style
  adds (`LightPatch`); the console works those words out again, in the
  level's own geometry stream, whenever a style's strength changes
  (`shared/HlLightStyle.hpp`). A named `light` is a thing the map's
  switches can use. (People standing in such light are not relit by it.)
- More of the game's machinery: a `func_train` keeps the angles it is
  given (the tram's door at the station is a quarter turn round from how
  it was built); `func_plat` and `func_platrot` (the round lift down to
  the test chamber travels and turns, and whoever rides it turns with
  it); lifts and trains have their going and stopping sounds;
  `func_pendulum`; a named `infodecal` leaves its mark when triggered
  (the microwave's last act); `env_beverage` (a can, and a point of
  health, while the machine has any); `env_funnel`.
- The picture is the player's to tune: OPTIONS > GRAPHICS in the pause
  menu (`shared/HlFog.hpp` has what each choice stands for).
  - **Fog**: *off*, *when needed* (only while a view too heavy to draw
    is being cut short -- what the fog was always for; in a heavy map,
    one whose worst view is over a pass's budget, it is always on,
    for most of its views are cut) or *always* (a steady distance fog).
  - **Strength** is how solid it gets at its far end; it used to be
    solid black right up to the cut, a wall: it now comes on along a
    curve and stops at seven tenths unless asked for more.
  - **Distance** is where an always-fog is whole (768 to 6144
    units) and how close to the cut the other kind is allowed to sit.
  - **Colour**: black, grey, blue, brown or green -- the empty distance
    behind everything takes the same colour, so a cut does not show as
    an edge.
  - **Model detail** (low / normal / high): how far off people and
    monsters step down to their coarser meshes -- half, as cooked for,
    or twice the distance.
  - **World detail**: how readily small far surfaces are left out when a
    frame is heavy -- sooner, as before, or never (then the view is cut
    shorter instead).
  - **Models at once**: 4 to 12, or what the machine is good for (8 on a
    DS, 12 on a DSi).
    Who those are is decided by what they are before how near they are
    (`shared/HlDrawPlan.hpp`): an enemy alive and in view is always drawn,
    limit or no limit, and so are the people of the place; what gives way
    for them is, in order, bodies and furniture, things to pick up, the far
    enemies' detail, the detail of the weapon in hand, and at the last up
    to 300 polygons of the world's far end. When that has had to happen
    the frame's stains, pieces and gibs are cut back as well. And "in
    view" follows the shape of the thing: a barnacle is in view by any
    part of its tongue, not only by its mouth on the ceiling -- under a
    high ceiling it used not to be drawn at all unless looked up at. The
    debug overlay says when anything in view was left out, by kind
    (`cut A0 B0 C1 D3`: A, enemies, is never to be anything but 0).
  - **Draw distance**: far (8192 units), medium (3072) or near (1536).
  The hardware's fog is 32 steps of density over a band of fixed widths:
  a light or near fog shows its steps.
- Textures that change, the game's four kinds:
  - **Cycling** (`+0name` .. `+9name`): screens and panels that play
    their pictures ten a second. The level lists a cycle's pictures one
    after another, all cooked at one size, and surfaces name the first;
    which is bound is the console's to say (`TextureCache::shown`). The
    geometry carries no texture choice of its own, so it costs nothing to
    draw -- only the pictures' video memory, which the budget counts.
  - **Switched** (`+0name` / `+Aname`): the other face of a brush -- a
    light fixture switched off, a button pressed in, a health or suit
    charger with nothing left -- shown by what the thing is doing
    (`BrushState::frame`, worked out each tick).
  - **Sliding**: a `func_conveyor` carries whoever stands on it at its
    speed, runs the other way when used, and its `scroll` texture slides
    with it through the texture matrix. (The same texture on a plain wall
    stays still, as in the game.)
  - **Varied** (`-0name` .. `-9name`): each face wears one of the
    texture's variants, by where the face is, so big floors and walls do
    not visibly repeat. Decided by the converter; at run time they are
    ordinary textures. It costs about a hundredth more surfaces and some
    texture memory; `--plain-tiles` cooks without it. The choice is not
    the game's own (that comes from the C library's random numbers): the
    same idea, another scatter.
- What a shot, a blow and a fall do, as in the game:
  - **Marks on walls** -- bullet holes, the crowbar's dent, blood behind
    what is shot, struck or blown up and under what dies or lands in pieces
    (red for people and the barnacle, yellow for the aliens, none for
    machines; the player's own when they are hit), the scorch of a blast,
    cracks on glass that is shot and stands. Scientists and guards can be
    hurt and killed, and the scene that needed them goes without. The
    stains the map's makers left (its unnamed `infodecal`s) are on the
    walls near the player: the nearest sixteen on a DS and thirty-two on
    a DSi within 640 units, each as large as its picture is in the game
    (48 units for most blood, 96 and 80 for the pools, 64 for a hand's
    print, 96 for a scorch or a crack), each one itself -- two side by
    side are never made one -- and the walls' own cracks and dents among
    them. They are the game's own decals (your `decals.wad`, cooked to
    one 256x64 texture), each drawn as one quad in its wall's plane with
    the hardware's "same depth" test, which is what that test is for. 32
    of them on a DS, 64 on a DSi; one on a door or a pane goes with it.
    They are kept cheap: a hole or a dent where one already is takes that
    one's place (ten blows on one spot are one mark, a pane has one
    crack, glass that never breaks takes none from a blow), and blood or
    soot only on top of its own kind. When there is no room, what gives
    way is what is least worth keeping -- a hole before blood, blood
    before a scorch, and the map's own marks never -- not merely the
    oldest, so a burst of fire does not wipe the blood off the walls. Each
    fades over its last three seconds, after twenty or forty seconds (twice
    that on a DSi); only as many as there are are counted against the
    frame, and no more are drawn than cover about three fifths of the
    screen between them on a DSi (a third on a DS) -- counted as they are
    seen, a stain on the floor ahead as the sliver it is -- those that the
    room runs out for, and those far off, thinning out rather than going
    all at once. Dust flies off a wall that is hit, and now and then the
    round whines away.
  - **Fading** -- as in the game (SUB_StartFadeOut), the pieces of a body
    fade over their last two seconds, and so do the bodies the game fades:
    a monster a maker made, and one the map marks to (SF_MONSTER_FADECORPSE),
    over 3.6 seconds once it has fallen; then its place is free. A fading
    model is drawn see-through under a polygon ID of its own, its back
    faces left out.
  - **Breaking** -- what breaks throws pieces *of itself*: small tumbling
    patches of the broken brush's own texture (glass see-through), as
    many and as heavy as its material says, which fall, lie and go. No
    shard models, no extra video memory. Each piece and speck is
    one triangle; the solid ones are sent in runs that share everything
    (the plain ones in one, each texture's together) and only broken
    glass takes a turn among what is see-through. They are gone in a
    second or two (half as long again on a DSi), sooner once they lie,
    and the far ones are not drawn at all when a frame is heavy.
  - **Being hurt** -- the screen reddens (a red sheet, as the game does
    it), a mark on each side of its middle the blow came from, and the
    head is knocked back. Falls hurt by the game's rule (safe to 580
    units a second, fatal at 1024). Dying, the camera sinks to the floor
    and rolls over before the game goes back.
  - **The view** -- each weapon's own kick when it fires, the bob of a
    walk and the lean into a sidestep (the game's numbers; off unless
    "View bob" in the options turns them on), the dip of a landing, casings thrown
    from the pistol, the MP5 and the shotgun (`shared/HlView.hpp`) -- and
    from the soldiers' guns and the assassins' pistols, as the game's
    EjectBrass throws them. A casing is the game's own model of it
    (`models/shell.mdl`, `shotgunshell.mdl`), tumbling as it flies and lying
    as it rests, with its own ring for each kind; where a frame has no room
    for them (a sixth of the models' share, nothing under pressure) or a map
    has not got them, it is a small triangle in its colour instead. A
    grenade in flight is the game's model of it too: a hand grenade
    (`w_grenade.mdl`, the player's and the soldiers') spinning through its
    "toss" in the air and its "roll1" along the floor as fast as it rolls,
    and the launcher's (`grenade.mdl`, the MP5's and the soldiers') pointed
    along its flight and tumbling end over end.
- Two things that could hold the game for good are closed. A player
  stuck fast in something (nothing got them out but a few units' nudge)
  is, after half a second, put somewhere clear up to 128 units off. And
  dying goes back to the most recent save of any kind -- but three deaths on end, each
  within twenty seconds of going back, go back to the save made on
  coming into the map instead (a second save file), and failing that to
  the map's start: a save made a moment before certain death is no
  longer a loop with no way out.
- The game's writing over the picture: the HALF-LIFE title on the first
  map, the names of the introduction along the tram ride, each chapter's
  name as it begins. The converter reads the game's `titles.txt` (what
  each message says, where, in what colours, how it comes and goes) and
  cooks every `env_message` and the world's `chaptertitle` / `gametitle`
  into the level; the console writes them on sprites of the top screen,
  in front of the 3D picture and added to it as the game adds its own
  (`source/render/TopText.cpp`: video bank G, no texture memory, the same
  in all three rendering modes), in the game's title face, faded or
  written out letter by letter as the file asks.
- The suit's lamp, by the game's rules: it wants the suit; two minutes
  alight, twenty seconds to be full again; it starts at 99 of a hundred,
  lights with whatever is in it, and what is in it is kept from map to
  map and in a save. Its light is the game's too: a ball eighty units
  from middle to edge at the point the middle of the view meets (out to
  2000 units), the same size however far off, fading evenly from its
  middle, full strength to 500 units and fainter beyond. The world's
  light is baked into its vertices and cannot be relit, so the light is
  *projected*: the surfaces in it are drawn a second time with one small
  round picture of light, and the hardware itself works out where on
  that picture each vertex is from where the vertex is in the world (its
  texture matrix, in the mode that takes the vertex --
  `NEA_TEXGEN_POSITION`; `WorldDraw::lampPass`, `shared/HlLamp.hpp`). So
  it lies on the walls as a light does: round a corner or a pillar, long
  on a floor it skims. The surface's own packed geometry is sent again
  through a small filter (vertices as they are, the projection's
  constant for a texture coordinate, and for a colour the wall's own
  texture's average, so that the wall is lit towards what it is and not
  towards white) and drawn only where the depth is the very same. Doors
  and other solid brushes are lit the same way; whoever stands in the
  light is lit by a hardware light along the view. What it is not: added
  light (the DS blends towards a colour -- the wall's texture is seen
  fainter in the light's middle, where the game shows it brighter), or a
  caster of shadows. Its button is in the touch screen's column between
  JUMP and DUCK, with what is left in it; Select switches it too; the
  game's lamp icon is at the top right of the top screen whenever the
  suit is worn, dim while the lamp is off.
- Everything on the touch screen is drawn in the game's own fonts, which
  the converter cooks from your `fonts.wad` and `gfx.wad` (`ui/` on the
  card; `shared/HlFont.hpp` draws them on a screen of tiles). The libnds
  text console is gone. A card cooked before this has no fonts: then the
  same code writes in the 8x8 letters that are in the ROM anyway.
- A head-up display on the top screen, as the game has it, in its own
  digits and pictures (its 320-wide set, cooked to one small texture):
  health and armour at the bottom left, what is in the gun and what is
  carried for it at the bottom right, the MP5's grenades above that, a
  crosshair for what is aimed. Nothing, until the suit is worn. It is
  drawn as flat quads six units in front of the eye through the world's
  own projection, which is right in all three rendering modes.
- Loading is still: the last picture of the game stays on the top screen,
  frozen, until the next map's first picture is whole (`FrameFreeze`). The
  3D picture left on the screen is drawn by the hardware again every frame
  out of video memory, and a load writes over video memory: so the screen
  is captured first and the capture shown in front of the 3D layer. That
  takes video memory, Bank D, which textures do not use (every map
  fits without it). The very first load has no picture to keep and shows
  the game's own loading picture (cooked from your `cached.wad`).
  That first picture is itself captured and its texture let go, so nothing
  drawn out of texture memory is on the screen while a map's textures are
  written into it. (The engine used to switch all four texture banks off
  for every texture it loaded, the one being shown among them: a band
  across the picture a texture. It now switches only the bank it writes.)
  The screen is blacked out for the dozen frames the new map takes to put
  itself in order. One owner of the screen's brightness (`ScreenFade`)
  keeps the map's fades, the dimness of being under water and this
  curtain from undoing each other.
- Levels of detail for models, made with meshoptimizer
  (`thirdparty/meshoptimizer`; `tools/hlds-convert/core/src/ModelMesh.cpp`).
  A person is 600 to 850 triangles in the game and the DS draws 2048 a
  frame, so every model is cooked at three levels: about 350 triangles
  (what it always was thinned to), half of that, and about a third. The
  simplifier weighs texture coordinates and normals with the shape and
  only ever keeps vertices the model has, each on its own bone at its own
  texel, so skinning and the skin's seams are as they were; each level
  records how far it is from the game's own mesh (the distance all but
  three in a hundred of its vertices are within). The console draws each
  model at the coarsest level whose error is under a pixel and a half at
  its distance, a level coarser for each step of pressure for all but the
  nearest, and coarser still before it gives a model up for want of room:
  a far soldier costs 120 polygons where he cost 350. A model small to
  begin with has fewer levels, or the one.
  A DSi's model files have a fourth level on top: the game's own mesh,
  untouched, for the nearest model (and the weapon in hand) in a frame
  with room. A DS has not the memory for a second copy of every mesh.
- Strips. The DS has no index buffers: a vertex costs each time it is
  sent. Every model's triangles -- thinned or not -- are made into strips
  afresh, by meshoptimizer's strip builder and by a plain one, and whichever
  sends fewer vertices is kept per mesh (the game's own, with their fans,
  if neither does better). About 1.5 to 1.6 vertices a triangle, where a
  model as the game has it sends 1.6 to 1.9: a tenth to a sixth fewer
  vertices for the same picture.
- One ROM for DS and DSi; DSi mode uses the 134 MHz clock and 16 MB of RAM.
  What a DSi has over a DS, apart from speed: sounds at the rate the game
  has them (up to 22 kHz, two megabytes a map: the converter writes those
  beside the DS's own, and says how many); twice as many effects, things in
  flight and pieces at once; twelve models drawn at once where a DS draws
  eight; the stereo music and room echo of its DSP,
  when that is switched on. Nothing is left out for a DS that it had
  before: the heaviest soldier maps (c1a3a, c2a2b1) leave it 1.6 MB and more
  free.
- The controls (`shared/HlControls.hpp`). What is held this frame goes in
  and what the player is doing comes out, by one function for both layouts,
  either hand and the two swaps of pad and stylus; the console's part
  (`source/game/Input.cpp`) only reads the hardware. So the host tests run
  it (`test_controls`): the classic layout is held, frame by frame over
  fifty thousand frames of random pressing and dragging, to the code the
  game had when there was one layout; the modern one, the hand and the
  swaps to what the README says of them. A tap is a touch of the aim pad
  lifted within a fifth of a second that went less than six pixels; it is
  USE as it lifts, without waiting to see whether a second tap makes it a
  jump -- waiting would make every USE a third of a second late. Settings
  saved before there were layouts are still read.
- A game between consoles (`shared/HlNet.hpp`, `shared/HlNetGame.hpp`,
  `source/net/`). Each console runs the whole game for its own player, as it
  does alone, and tells the others; none of the ninety-odd functions that
  take "the player" had to learn that there are several.
  - **The radio** is dswifi's local multiplayer (the fork that also does
    Download Play): one console the host, up to three its clients. Every
    frame the host sends one frame all clients hear, and each client answers
    with one the host hears. Nothing else is used during a match.
  - **Online** (a DSi's: MULTIPLAYER, ONLINE), **the relay** stands in for
    the radio (`tools/relay`, `shared/HlRelay.hpp`, `source/net/NetRelay.cpp`).
    A host opens a room on it, a console looking is given the list of rooms
    in place of the beacons it would hear, and asks to join one: the host
    is asked, by name, whether to let it in (the lobby's LET ... IN? YES
    NO; no answer in half a minute is a no); then the relay
    passes the host's frames to each client of the room and a client's to
    the host, saying whose, as the radio does. It never reads them: the
    lobby and the match are the same, and `NetLink` hands its calls to
    `NetRelay` while online, so nothing above it knows which it is. UDP,
    over one of the console's saved Wi-Fi connections (DSi mode, so WPA
    too: the radio's local play and Download Play are not there then, and
    the Wi-Fi is restarted between the two). The relay's address is the
    card's `server.ini`. The IP stack (lwIP, in the wireless library) is
    about 90 KB: `tools/wifi_twl.py` moves it, and `NetRelay`'s own code and
    buffers, into the DSi-only part of the program, which a DS never loads.
    The library's flag for "the IP stack is on", which its code every
    console runs reads, is the program's own (`NetLink.cpp`), in memory a DS
    has. What is left costs a DS about 3.4 KB. The stack is a cooperative
    thread of the library's, run only when the program yields:
    `swiWaitForVBlank` halts, so `NetRelay` yields each frame. On the ARM7
    the DSi's wireless is driven from the program's own loop, a few packets
    each way after each sound block (`Wifi_SetUpdateFromLoop`, in the
    `dswifi_dl` fork), not from its 1 ms timer, card interrupt and FIFO
    messages: with a game sending many packets one of them was always due
    again, and the ARM7's sound, buttons and touch stopped for good. (Its
    start-up still runs with interrupts off: it shares the I2C and clock
    buses with the ARM7's interrupts.) The sound's own clock is timer 1
    alone; timer 2 is the wireless's. Going
    online is a step a frame (CONNECTING), never a frozen menu. An online
    lobby offers nothing by Download Play: no guests, no SEND GAME. Not yet:
    nothing checks who a console is.
  - **The host's map, sent** (`source/net/MapShare.cpp`), on the radio and
    online alike, to a client with a card that has not got it, or has
    another version of it. Maps go by their name's hash, so the host says
    what its map's archive comes to (MapSum: a crc32 of the map's name and
    its `maps/<map>.npac`, worked out a slice a frame in its lobby, with
    each member's own crc32 as it goes by); a client checks its own the same
    way, or finds it among those it was sent before
    (`maps/net/<map>-<crc>.npac`), and says what it has (MapHave). It is
    sent when the match begins, as the game is sent to the consoles given
    it by Download Play: the host stays on its STARTING page ("Sending
    <map> to 1 console: 45%", each player's "receiving") until nobody is
    still getting it -- one silent ten seconds is not waited for -- and
    loads its own after; the client shows what a console given the game
    shows. Most of an archive is not the map's own: the weapons', the
    players' and the monsters' models and sounds are in every map's (bounce
    is 2.4 MB, 2 MB of it found on any card). So the client is sent the
    archive's index first (`FetchFrom::MapIndex`: its tables, read by
    `shared/HlNpacIndex.hpp`, and each member's crc32); reads the indexes
    of the archives on its own card, its own version of that map first;
    copies each member found with the same path, length and packing, and
    keeps it if it comes to the host's crc32 (another map's `level.0` is the
    same length, not the same level); and asks the host for the rest, as
    anything else is asked for (`FetchFrom::MapCard`), its FetchHave saying
    it has the pieces it copied, so that the host's window passes them by.
    What comes is written where it goes, through a buffer, and all of it is
    checked once it is whole: wrong, it is sent again whole. A client
    answers each piece with a small frame of its own (`kNetFrameFetchHave`,
    in place of the game's, as a guest does) rather than an event, which
    would wait behind the game's. What was sent is never put over the
    player's own map: it is played from where it was put
    (`Global::setMapFile`), and kept, one version a map, so the next time
    nothing is sent. A DSi's alone: the code and its tables are in the
    DSi's memory, and a DS goes by the map's name as before.
  - **Chat** (a DSi's: CHAT in the lobby's bar and on a match's pause
    page, the menu's own keyboard, the match going on meanwhile). A line of
    up to 59 letters goes as Chat events, twelve bytes each, in order and
    once like every event; the host passes a client's on. What was said is
    kept in the DSi's memory (`NetChatLog`, which the match only points to:
    a DS keeps none, offers no CHAT, and ignores what it hears), shown in
    the lobby's box for a while and, in a match, low on the picture as
    "NAME: text", apart from who killed whom. One line every two seconds a
    console.
  - **A state** (sixteen bytes: where, facing, how fast, what in hand,
    standing or crouched or dead) goes whole in every frame, so a lost frame
    is replaced by the next. **An event** (a shot, a hit, a death, a pickup,
    a button used) is numbered, sent in every frame until the other end says
    it has it, and taken in order, once. A client's events go to the host,
    which passes on what the others need in its own, one stream that every
    client follows from where it joined.
  - **A hit is claimed by whoever made it.** The others are actors of a new
    kind (`ActorKind::Player`) on each console, so the game's own
    `fireLine`, hit boxes and `radiusDamage` find them; what is taken off
    such an actor is not a wound but a claim, sent to the console whose
    player it is, which takes it (`playerHurt`). A console's word is taken
    for its own position and its hits: right for friends in a room, and what
    lets the game stay as it is.
  - **The host decides what is shared:** who has a pickup when two reach for
    it (touching one only asks for it), when it is back, the scores, the end.
  - **The map's machinery** runs on every console. Each works out, for every
    player, what they walk into here (`logicGhostTouch`: the triggers,
    the doors and buttons that go at a touch, the lift stood on); what a
    player USES or breaks is an event, done again on the others by someone
    standing where they stand. There is no periodic check yet that the
    consoles' doors and lifts agree.
  - Played also between two linked melonDS instances (`tools/emu`, two
    consoles): lobby, a match, hits and a kill seen and scored on both, the
    respawn, pickups, two minutes of both moving and firing, and each side
    leaving. A hit carries which of the victim's lives it was aimed at, and
    a dead player is not shot at: shots at a body used to be taken by the
    life after it.
  - `test_net` plays it: events through air that loses a fifth of the
    frames and delays the rest, with a client leaving and another coming;
    a lobby with no map loaded; and three whole games on crossfire -- seen
    by each other, hit, killed and scored, back after three seconds, two
    reaching for one shotgun, a button used, a trigger walked into, one
    leaving, the frag limit. Also the sending of a file in pieces (only the
    pieces the other end lacks), which is for consoles started by Download
    Play, when that is built.

## The game as a library

The program is cut where Half-Life cuts it, between the engine and
`hl.dll`. The game -- the monsters and their thinking, the weapons, the logic
entities, a map's saved state, the changes of map, the multiplayer match's
rules -- is a BlocksDS dynamic library, `dsl/hl.dsl`, built from `dll/hl/`
(and the shared headers it instantiates) against the ARM9 ELF and carried in
the ROM's NitroFS. The engine loads it at startup (`source/game/GameDll.arm.cpp`)
and from then on the two speak through two tables
(`source/game/GameApi.hpp`):

- `GameFuncs`, what the engine asks of the game (`DLL_FUNCTIONS`): `Game`'s own
  functions -- set a map up, advance it, save it, change it -- each a one-line
  hand-on in `source/game/Game.cpp`; the think worker's question; fall damage;
  `killall`; the match's start, tick, events and USE.
- `EngineFuncs`, what the game may ask of the engine (`enginefuncs_t`): its
  objects (`game`, `camera`, `player`, `sound`, ... -- the library's
  `HL_ENGINE_OBJECT`s are references to them), the twenty-odd of their
  functions the game calls, and the hot code in ITCM -- `traceHull`,
  `traceAll`, `poseModel`, `actorAdvance`, `walkTick` -- whose declarations
  are bodiless in the library's build (`HL_DSL`).

The engine owns the memory, as it owns the edicts: the renderer and the sound
read `game` as they always have. The library binds by address to nothing of
the engine but libc's, libnds's and the compiler's functions and the table
itself, `hlEngine`; a handshake compares the API's version and the sizes of
two dozen structures both sides lay out, and refuses a library of another
build. Saves are as they were: a save written before the split loads.

What it costs and what it took:

- Heap on the DS beyond the code itself: the loader keeps 8 bytes per
  relocation for branch veneers (none is ever used: no jump goes to Thumb
  code) and the whole symbol table, for as long as the library is loaded.
  dsltool's file (`build/hl/hl.full.dsl`) names every symbol a relocation
  points at and lists every branch within the library, which the loader
  leaves alone; `tools/dsl_strip.py` keeps only what the loader acts on --
  the words it moves, the 23 symbols of the program and the 6 public ones --
  and the build stops unless the result loads to the same bytes, with the
  same sum for the guard, as dsltool's (2026-11-08: 6,701 relocations to
  3,303, 1,109 symbols to 30, 88 KB more heap, measured on c0a0 in DS mode).
  Frames take what they took (c1a1 with a zombie: DS 52-56% of the frame,
  before 55-60%; DSi 36%, as before).
- The library is ARM throughout. An ARM-to-Thumb tail call inside it goes
  through a stub the linker writes with an absolute address and no
  relocation: the loader cannot move it, and the call lands where the library
  was linked (a hang on the DS, an abort on the DSi).
- The loader's branches reach 8 MB, not 32 (libnds checks a byte offset
  against a word one's limit), and ITCM is linked 17 MB below the library.
  The hardware divide the library calls is therefore called at ITCM's mirror
  just under main RAM (a symbol resolver), with the protection unit's ITCM
  region opened over the mirrors. The library is loaded before anything else
  takes the heap, so that it sits next to the program.
- Its build is given `__NDS__`, `__BLOCKSDS__` and `ARM9` as the program's
  is: libnds's and the engine's headers lay their structures out by them.

`tools/arm9-report.sh --check` holds both to this: no Thumb code and no
linker stub in the library, nothing of the engine taken by name, and none of
the game's code left in the program. It also holds the library to what the
loader (libnds `dlfcn.c`) silently gets wrong: no section aligned on more
than eight bytes (the block is malloc's), one relocation section (only the
first is applied), no word pointing *into* something of the program (the
addend is dropped); and `hlDslData` where the library's data begins.

### Guards, and the crash screen

`source/Guard.hpp`. Added 2026-10-29, after a data abort on a DSi turned out,
with the ELF of that build rebuilt, to be `free()` tripping over a heap
already broken (an archive file being closed: dlmalloc merging with a free
neighbour whose links had been zeroed) -- the culprit long gone.

- **The heap.** Every malloc, calloc, realloc, memalign, aligned_alloc,
  posix_memalign and free -- the program's by `--wrap` (build.py), the
  library's by the loader's resolver (GameDll) -- goes through the guard.
  - **In every build**, the allocator's own chain is walked at each map load
    (old map gone, new one in) and save: libnds's dlmalloc keeps a size word
    in front of every chunk, chunks follow one another from the heap's start
    to the top chunk, and two free chunks are never side by side. A write
    past a block's end lands on the next chunk's size word, and the chain
    no longer adds up: the game stops on the crash screen with that chunk,
    the word found there and the chunk before it (whose end it was). It costs
    nothing per block. The same walk gives `heapRoom()`, the `room:` line of
    `perf.txt`.
  - **A debug build** (`HLDS_DEBUG=1 ./make.sh`, `HLDS_GUARD_FULL`) adds the
    full guard: a header in front of every block (its size, the address of
    the code that asked for it) and four bytes after its end. Freeing checks
    both; small blocks (up to 4 KB, 32 KB of them in all) are then held a
    while, filled with a pattern that must still be there when they really
    go. A write past a block's end, into one given back, a free of something
    that is not a block or of one twice: the crash screen names the block,
    its size and who asked for it. Cost: 24 + 4 bytes a block and the held
    blocks, 10 to 16 KB on the tightest DS maps, which is why a release
    build is without it.
  - NEA's video memory lists take no blocks during play: each
    split of them needs a node, and the nodes come from a reserve of 1,024 set
    aside at start-up (`Global.cpp`; 548 at most were measured, on
    rocket_frenzy on a DSi; not in the Download Play child, whose memory was
    measured without it). One from the heap took 48 bytes and a piece of
    it each time; c2a5e did not start on a DS before.
- **The stack.** Sixteen words at the bottom of the stack's room in DTCM hold
  a pattern, looked at every frame.
- **The library's code** (and constants) is summed once loaded and at each
  check: a write into it stops the game.
- **The crash screen** (in place of libnds's): the build's name
  (`tools/buildid.py`: commit, and `+` the time if the tree was not clean),
  what happened, pc and lr placed (`main`, `dsl+offset`, `itcm+offset`), the
  registers, the code addresses found on the stack, and the three checks.
  `make.sh` keeps each build's ELFs in `build/elf/<name>/` (the last twelve):
  `arm-none-eabi-addr2line -f -C -e build/elf/<name>/arm9.elf <address>`; an
  address in the library is `hl.elf` at its offset.

## Achievements

The 68 achievements of Half-Life: Legacy (a mod on Steam, app 3619040), with
the mod's names and descriptions, earned in Valve's own maps. They are one
set for the console (`data/achievements.dat`), not one per save: earned once,
kept through every new game, and what counts towards one (500 headcrabs, 250
broken things) adds up across all of them. Not in a game between consoles.

- **What earns them** is `shared/HlAchievements.hpp`: the table, the file,
  and the rules, by map. A chapter is done when the way out of its last map
  is taken into the next chapter's first (worldspawn's `chaptertitle` marks
  those; its `message` is wrong in places). The rest is a name used in a map
  (a button's target, a relay, a trigger), a monster killed (by the player,
  for most), an item taken, or a place stood in.
- **What the game reports** goes through `shared/HlAchEvents.hpp`, a short
  list the logic adds to and the engine empties after each frame: in
  `logicFire` (a name the map's rules listen for, and only when something
  there by that name answered: c5a1's `loser_mm` is still fired at nothing
  after the offer is taken), at a monster's death (`aiTakeDamage`), a
  turret's (`monsterHurt`), a pickup, a break, a soda. `killall` is muted.
- **The engine's half** is `source/Achievements.cpp`: the file read at boot
  and written when one is earned (counts alone at a change of map or the end
  of a pause), the list handed to the logic at each load, and each one earned
  shown on the top screen with a trophy (`TopText::announce`), one at a time.
  The COMMAND line's cheats earn the hidden Cheater and spoil nothing else.
  The pause and title menus have an ACHIEVEMENTS page:
  - a chapter at a time, in the order they are played, then Uplink, then
    the general ones (`kAchByChapter`)
  - each achievement a row to touch, with its trophy and how far along it
    is; the box under the list reads out the one touched
  - the list slides aside (or < >, L/R) for another chapter, and drags up
    and down when it is longer than the room
  - a slider dragged ticks once where it comes to rest (and on letting go
    before that), not at every step it passes -- the weapon list's rule
    (`hldata::UiSettle`)
  - POP-UPS turns the top screen's pop-ups off: achievements are still
    earned and listed (`settings.dat` version 12)

Where the mod's own maps differ, the original's nearest is used:

- **Hidden places** are boxes, found from what lies in them:
  - Barney's room (c1a2d), the maintenance area's turret room (c1a2a), the
    grenades (c1a2b)
  - the chargers between the conveyors (c1a3a) and by the barnacle (c1a4i)
  - the cache by the lift (c2a2b2), Apprehension's past the grate (c2a3a)
  - the room behind the snark doorway (c3a1)
  - the big lift's foot in Lambda Core (c3a2e)
  - the foot of Interloper's spiral (c4a1e)
  - the scientist on his pillar (c4a3)
- **Signature move** is the room under the stairs in c2a2c, whose wall reads
  RILLER in brushes. No decal is a signature.
- **Defiant** is Barney's telling-off for heading to the test chamber without
  the suit (`hevnotrigger`).
- **Got the Chills** is the wheel that opens the freezer (`freezdor`, c1a2c).
- **Better than a Snake** asks that every tripmine c2a5d lays is still on its
  wall when the lift is called.
- **Owowowowowow!** counts the floor's turret (`monster_sentry`); the others
  hang from ceilings.
- **Marathon** is the Hazard Course, Uplink (hldemo1-3) and the game, each
  to its end, in any order.

`test_achievements` tries every rule. With `HLDS_VALVE_DIR` set, it also
checks that every name, monster and place is in the map it is given for, and
plays a few: the hand dryer, `loser_mm` after the offer, the guard in c1a1.

## Widescreen on a 3DS

On a 3DS, a DS(i) program's screens are shown by TwlBg, a 3DS-side module
loaded from the console's own firmware.

**The wide TwlBg.** TWPatch makes one from the owner's firmware. It shows
the top screen's 256 by 192 pixels at 384 by 240 (×1.5 across, ×1.25 down),
a 16:10 picture. TWiLight Menu++ swaps it in for one launch, and only if two
things hold:

- the ROM is a homebrew marked as able to draw for it, by the game code's
  first letter `W` or ROM version `0x57` (`build.py`: `WHLD`, `0x57`);
- its per-game Screen Aspect Ratio is 16:10.

**Drawing for it.** The game draws through a 1.6 projection
(`NEA_SetAspect`, in the Nitro Engine fork) and keeps the 4:3 picture's
height, so a field of view of 90 shows 101 degrees across
(`Camera::setWide`). The rest follows the projection:

- **cull planes:** worked out in fixed point, with no double-precision
  library, which would cost a Download Play guest 3 KB;
- **crossbow sight:** its planes are widened the same way;
- **crosshair autoaim shift:** follows the wider view;
- **flat pictures drawn in 3D** (loading screen, logo): drawn with a
  horizontal pixel 6/5 of the vertical one, so they cover the same pixels as
  on 4:3.

The HUD's sprites and the 2D menus are stretched with the rest of the
screen, as in retail widescreen patches.

**Knowing which TwlBg is loaded.** TWiLight puts the files back before the
game starts and tells a homebrew nothing. The game asks the 3DS instead,
through TWPatch's **rtcom**: the TwlBg's per-frame hook reads two of the
emulated clock's registers, which the ARM7 reaches through the clock's
serial port with commands sent high bit first. The ARM7 does this once at
start, before the sound (`arm7/source/Rtcom.cpp`):

1. TWiLight's MCU test (register `0x71` does not keep a write on a 3DS). It
   runs in DSi mode only: a DSi's or a DS's clock is a real chip and is
   never sent rtcom commands.
2. An unknown command, which a TwlBg with rtcom answers `0x82`.
3. An upload of `Peek11.s`, 88 bytes of ARMv4 that the ARM11 also runs.
4. A read of the top screen's window edges at `0x1ED02218`/`0x1ED0221C`
   on the ARM11's side: 320 pixels wide on the plain TwlBg (measured on an
   Old 3DS: `0x28`..`0x167`), 384 on the wide one.

The answer goes to the ARM9 once, on `FIFO_USER_02`. The screen setting's
"auto" follows it, and "4:3" and "16:10" override it. The probe that found
all this is `tools/hwtest/rtcom`. The wide TwlBg's window has not yet been
read on hardware; the 384 comes from TWPatch's source.

## Not yet

Friends who follow and talk; the rest of the game's weapons;
`env_bubbles`; the suit's voice, the chatter of scientists
and guards; a track train's starting sound. Soldiers fight the player only
(not the aliens), do not drop their guns, and are
not eaten by barnacles; the player cannot yet lay a tripmine. What breaks
still comes apart into plain scraps (it is monsters, and what the maps throw
out, that have the game's pieces). Scientists and guards do not
think: they stand where the map puts them and go, in a straight line, where a
script sends them. A script whose actor this port does not have is taken as
played after a few seconds, so that what waits on it still happens. Monsters
do not open doors or jump gaps. A map that is come back to starts as it
started, except that what was broken, killed or picked up stays so.
The converter counts what it does not handle rather than refusing a map.

## Tests, the DSP program, timing

Set `HLDS_VALVE_DIR` to a Half-Life `valve` folder to make the tests also
check collision and movement on a real map, run the whole tram ride through
its six maps, play the resonance cascade through to the map after it, fight
headcrabs and zombies in theirs, check that nobody in twenty-three maps
stands or walks inside the world, and pack every common sound (under a
minute, nothing is drawn or heard).

The DSP program needs the Teak LLVM toolchain and libteak in `$BLOCKSDSEXT`
(`llvm-teak/`, `libteak/`); `./make.sh` builds it and links it into the ROM,
and without them builds a ROM with no DSP audio. `tools/dspsim/run.sh` runs it
against its model (it needs teakra, built: see the script), and says how much
of the processor it uses; it is one of the test suites, skipped where teakra
is not built.

A ROM built with `HLDS_PROFILE=1` shows where each frame's time went on the
debug overlay and, every 512 frames, writes a line to `perf.txt` on the card:
the mean and the worst frame of each part (logic, movement, choosing the
world's surfaces, sending them, posing and drawing the models, the whole
frame) in microseconds, how deep the stack has been, and how many divides
and square roots a frame asked of the hardware. An emulator's times are
only a guide: it does not have the console's caches, which is what the
placing of code in fast memory is about. The console's own `perf.txt` is
the number.

`tools/emu/` has scripts that run the ROM in melonDS without a display, for
looking at a change on the console's own renderer -- and, with `EMU_AUDIO`,
for writing what it would have played to a file.

## Comparing with the game

`tools/ref/` runs the real Half-Life -- your own Steam copy -- without a
screen, with the player put at a place and the game driven by the same kind
of script that drives the port in the emulator, and lays the two pictures
side by side:

```sh
tools/ref/compare.sh scientist c1a0 "-768 104 -180 0" "shot; key a 0.1; sleep 0.15; shot"
```

It runs the game out of a folder of its own beside `valve` (made by
`tools/ref/setup.sh`, removed by `remove.sh`), so your game's config and
saves are not touched. See `tools/ref/README.md`.

The crowbar was the first thing checked this way, and is now the game's: its
three swings at the air in turn and its two that land turn about, a blow on
a wall counted as a blow (it was a miss), every swing heard through the air,
a swing that meets nothing made again a tenth of a second on, a whole blow
after a second's rest and half of one on the heels of the last, three thuds
on flesh and two rings on a wall -- as loud as what the wall is made of lets
them be, with that material's own sound -- the dent a fifth of a second after
the blow, the hand left as its last swing left it, and put away before
another weapon comes out (`shared/HlMonsters.hpp`, `crowbarBlow`).

## The pass's budget

The hardware holds 2048 polygons and 6144 vertices a frame, counted after
clipping. Once full it drops every polygon sent after, and says so only at
the next blank (`DISP3DCNT` bit 13). The walk counts before clipping, and a
surface whole, however little of it is on the screen. So the two counts
differ, and the renderer has to live with both.

**Near first, far last.** The walk marks each surface it takes as near or
far: far when its nearest point (its middle along the view, less its radius)
is past half of how far the view reaches -- where the fog was whole last
pass, or, with no fog, the map's own farthest (`WorldDraw::kNearShift`).
The send goes through the near ones by texture, then the far ones by
texture (`emit`, `emitList`). An overflow then takes what is far, which the
fog is already fading, and never a near ceiling that happened to have the
last texture. What is sent after the world -- models, brushes, effects --
still comes after both, so an overflow in the far half takes them too: as
it did before. It costs an extra texture bind for a texture worn on both
sides of the line, and nothing measurable in the walk or the send.

**What the hardware really holds.** `perf.txt` gives, for each map, the
hardware's count less the walk's -- the most of it, and nineteen passes in
twenty -- and the most the hardware held on a pass the walk had filled
(`excess-poly-most`, `full-poly-most` and the like). In melonDS, over the
c1a0 hall, the c1a2 fight and c2a5's worst place (DSi; DS for c1a0), the
hardware held less than the walk counted on every pass: at least 155
polygons and 336 vertices less. At c1a2, cut and pressed on almost every
pass with the old budget, it never held more than 1562 polygons and 3883
vertices. So the pass's budget is the level's (`kPassPolygons`, 1700 and
5200) and 200 polygons and 600 vertices more at run time
(`WorldDraw::kHeadroomPolygons`): with them, c1a2's cut passes went from 19
to 9 of 1536, c2a5's pressure went away, and the most the hardware held
anywhere was 1778 polygons and 4149 vertices -- 270 and 2000 short of
full. `kPassPolygons` itself is left alone: it is what the converter calls
a map heavy by, and changing it would change which maps have the fog on
always. These are emulator figures: a console's own may differ, and are
what to set the headroom by.

**The governor's climb and the pressure's wait** were tried faster, and
are left as they were, as named numbers to try again on a console: the
climb towards what was counted and not used (`kBudgetClimb`, 10 polygons a
pass; tried at 30, with up to 900 polygons and 2200 vertices added in
place of 600 and 1500: `kBudgetMostSpare*`), and the most the pressure's
wait to come down may grow to (`WorldDraw::kRelaxWaitMost`, sixteen times
its start; tried at four). Neither measured any different at these places.
What the climb aims at is three quarters of what was counted and not used,
and that is small where a view is heavy; a faster climb only gets there
sooner. And at c1a2 the pressure stays because what is counted, with what
it leaves out, never fits in fifteen sixteenths of the budget: the wait
never starts.

**The box test** (an experiment: `HLDS_BOX_TEST=1 ./make.sh`). The geometry
engine can say whether a box is inside the view as it clips (`BOX_TEST`),
which for a flat surface is much tighter than its ball against five planes.
Each surface's box is worked out at load from its own commands
(`shared/HlGxBounds.hpp`), so the cooked level is as it was. It is in the
vertices' own numbers -- quarter-units, read as 4.12 under the world's own
matrix -- so the test is asked in the same space the polygons are drawn
in. Its corners are vertices, so they fit the test's 16 bits; only its
size might not, and on c1a0, c1a2, c2a5, c2a5x and c4a1 the widest is 4300
of 32767 (`test_gxbounds`). The test needs far-plane-crossing and one-dot
polygons kept, so the walk sets that format with an empty begin and end,
and puts the world's back after. It waits for the answer, so it is not
asked while a DSi's DMA may be feeding the geometry engine: `finishList`
has always waited for the list before the walk comes round, and the walk
checks the DMA as well (skipped: never, in every run). In melonDS it left
out 9 to 42 more surfaces a pass than the balls did, 2 to 5% of the
polygons counted, with the hardware drawing the same -- and the walk took
2.5 to 4 ms longer a pass on a DSi. It is off: melonDS is not the
console's clock for the geometry engine, and only a console's numbers
(the walk's time on the overlay, `perf.txt`'s `box-out-mean`) can say
whether it should be on.

## Streaming textures

A map cooked without `--no-stream-textures` (streaming is the default) has its plain opaque textures (one
picture, no holes, not a liquid, belt, or similar) sized for what **one view**
of the map lists and not for the whole map. They are cooked at the game's own
size (each side the nearest power of two, 128 at most) and marked `kTextureStreamed`.
If the worst view still does not fit what the 4x4 bank pair has left (after the
sky, the models' skins and the textures that are resident), the ones doing least
in that view are halved until it does (`TextureBudget.cpp`, `StreamOptions`).
The report says how many there are, how much they come to in all, what the worst view
lists of them and how much room they have. Everything else is planned as
before, within 40 percent of the texel budget.

The level (format version 27+) carries what the console needs: each texture's
**average colour** (`TextureRef::average`), the **share** of the map's
leaves that see it (`TextureRef::share`), and the most texels one leaf's view
lists (`LevelHeader::worstTexels`). Since format 50 each texture's record also
says how many **colours its palette** has (`TextureRef::paletteColours`), and a
streamed texture is given exactly that much palette memory. It is written when
the map is packed, from the very texture file packed beside it: the stage's
file of a name and size is shared by every map that uses it, and a map cooked
after may have made it again with another palette. Before, it was given what a texture of
its size might have at most, one colour for every 16 texels: on c2a5e that was
83 KB for palettes that come to 15 KB, in a palette area of 26 KB, and an
in-view texture could stay flat for seconds for want of a piece of it big
enough. A record that says nothing believable (not 1 to 512) gets the old
guess, and perf.txt says how many did.

On the console `TextureCache` does not load a streamed texture. It draws it
as a flat colour and `TextureStream` reads the real one in and out of video memory
as described in `shared/HlTextureStream.hpp`, testable on PC (`test_texturestream`).

- Every surface drawn notes its texture (`noteTextureDrawn`). One drawn in
  the last six frames is wanted above all else; one drawn in the last
  hundred and twenty a little. One on a world surface the eye's leaf can see
  (its PVS) is wanted a little less again. It is read ahead if there is room,
  and what cannot be seen from here makes way first. The PVS's textures are
  found again each time the eye enters another leaf, a piece a frame
  (`PvsTextureWalk`), from the leaves the renderer has just marked visible.
- What there is room for is found when the map has loaded (`begin`): what NEA
  has left after everything that is not streamed. The textures most leaves see
  (a share of 60 and up, as much as a third of the room) are put there for good,
  then the rest by share. A map whose streamed textures all fit has all of them
  there, for good, and never streams.
- At the end of each frame (`update`) the best-wanted textures that are not in
  memory are read from the card, a few kilobytes a frame (4 KB on a DS, 8 on a
  DSi). When there is no room, a resident texture makes way for one wanted
  more than twice as much; what is nearly as wanted is left alone, so that two
  textures cannot take each other's place for ever.
- Palette memory is not moved about to join its free pieces. A texture
  whose palette finds no piece big enough, even with what may make way let
  go, waits (drawn flat) until the view changes. How often that happened
  although the pieces came to enough in all is counted ("refused in pieces"
  in perf.txt, beside how full palette memory was at most). Measured on a DS
  on the maps with the most palette to stream (c1a1a, c1a0d, c1a3, c1a1f,
  c1a0b, t0a0b, c2a2b1, c2a5e: up to 99% full), it never happened once
  palettes were given their real size (format 50), so joining the pieces is
  left undone until a map shows it is needed.
- What is read is written to video memory by `VramWriter`, in the vertical
  blanks, when the 3D engine is not reading it, and each texture is drawn as
  soon as its own writes are done. Reading goes on every frame while earlier
  textures wait to be written, as far as the 32 KB staging buffer goes. A
  texture placed where another was let go waits two frames first, since the
  picture on the screen until then may still be drawn from there. One placed
  in memory free for longer is written at the next blank (the plan remembers
  when each piece was let go: `readyAt`).

The debug readout shows it: `tex 72/75 +63 -0 flat 0 210/211K` is textures
in memory of those streamed, loaded and let go since the map began, how many
seen lately are being drawn flat, and the room the plan counts in use of what
it counts there is.

## Weapon skins

Each map holds the skins of every weapon the player can hold, but only one
is in hand at once. They are loaded as RAM-backed NEA materials; the texture
is resident in main memory, and the palette stays in palette memory.
When the player takes up a weapon, `Game::takeInHand` asks `ModelCache::wield`
for it, and `ModelCache::apply` loads that skin into video memory, letting go
of the previous one. Weapons whose skins is not in video memory yet are not drawn.
Ones that do not fit are drawn bare, as any other skins that do not fit.

Skins' sizes are worked out per map. Each skin is cooked at the smallest
power-of-two atlas that holds its textures at three quarters of their width and
height or better, then the largest are halved until what resident in video memory
together fits `kMapSkinTexels`. When streaming, that limit then grows
by what the worst view leaves of the room for the streamed textures.
Across the campaign and test maps this took the models from about a third of
their source texels (58% of the width and height) to about 72% (85%).

`TextureStream::begin` leaves room for the largest weapon skin
(`ModelCache::reservedBytes`) before it shares out the rest. The converter counts
in a parallel way: models' video memory total is all base skins plus the largest
weapon's, so the "skins" figure in its report is what is in video memory together.

**Weapons a map has not.** The converter gives a map the view models of
what the player can hold by then (c1a1 has no MP5). A cheat, `give` or a
debug start can hand over any of them, so every map also names the view
models it lacks (`kModelRefLazy`, the last byte of the ref's name). Those
are cooked once, with the clips the game plays and a skin of at most
128x128 (`kLazySkinTexels`), and packed into `common.npac` under `wpn/`
(`wpn/<name>.hmd`, `.dsi.hmd`, the skin), not into any map's archive.
`ModelCache::wield` (an engine call from the game library, `modelWield`)
reads one when it is taken up, letting go of the one before (not the
tripmine's, which may be on a wall); with too little memory it stays
undrawn. The skin goes through the weapon slots as any weapon's: a map
naming one sizes its slots for `kLazySkinTexels`, and one more palette is
kept free for it. Its sounds -- the clips' (reload clicks) and the
weapon's own common sounds (an MP5's shot where the map has no MP5) --
are named in the map with `kSoundLazy` (the top bit of `SoundRef::twin`):
not loaded with the map nor in its archive, but read out of `common.npac`
the first time each is played, into one of four places (`Sound::lazyTouch`).
That first time may be a frame late. What a lazy weapon throws (a rocket,
a bolt, a snark, a satchel) is still the map's to have.

## What the converter writes

**On the card** a map is one file, `maps/<map>.npac`: an archive (Nitro
Engine Advanced's NPAC, mounted by the console as a drive) holding all that
the map loads -- its level, textures, sprites, models and sounds -- in the
order it loads them. Opening a file is the slow part of reading a card, and a
map used to be between one and five hundred of them. `common.npac` holds
what belongs to no map (the letters, the head-up display, the menu's
sounds, the weapons some map has not and their sounds). Loose beside them: the music, which is played off the card as it
goes, the manifest, and what the game itself writes.

Inside an archive, what shrinks well is LZ77-compressed (the DS's own BIOS
format, undone by the engine as the member is read): levels and models
come to about half. A member is compressed only if it is a kilobyte or more
and comes down to three quarters of itself; sounds, which are compressed as
sound already, are stored as they are, and so are a map's textures, which
are read during play as they come into view: the engine reads one straight
from the archive into where it goes, found in the archive's index with no
file opened (`NEA_NpacMemberRead`). Maps from before level format 49
compressed them; maps from before format 50 do not say their textures'
palette sizes. Both are refused: the console says so before anything is
loaded, and asks for the converter to be run again.
The map being played is mounted with its archive's index -- its files,
their names and how each is packed, 6 to 31 KB -- in a fixed buffer of 32 KB
(`shared/HlArchive.hpp`), not on the heap: one map after another goes into
the same memory. The converter warns about an archive whose index would not
fit, which is then mounted on the heap. Another map's header (for a save,
the menu's games, a change of level) is read from its archive without
mounting it (`NEA_NpacPeek`).
A level is in pieces of 256 KB at most, each compressed on its own, because
a member is inflated whole beside where it is going and a DS has not the
memory to do that to a level. `--no-compress` stores everything.

**Finding the way.** The game works out, the first time a map is run,
which of its path nodes can be walked between, by what size of creature and
through which doors, and writes that beside the map
(`maps/graphs/<map>.nod`); those files come with the game, and the converter
reads them (`NodGraph`): the nodes, the links for small things, people and
large things, and the door or breakable thing each link passes through. It
adds ways DOWN from ledges, which the game has not got. There is no table
of routes on the card: the console finds a route when it needs one
(`shared/HlWalk.hpp`, `pathStep`), over the links as they are at that
moment -- a door shut costs a little and is opened by whoever opens doors,
a locked one or something not yet broken is no way. Monsters go to where
they last saw the player, give up after a while and go home; soldiers take
cover they can reach. Scientists and guards follow the player on USE and
stay on USE again; a scientist runs out of sight of what would kill him.
A guard fights it, after the game's own guard: he keeps the enemy he has
until it is dead or six seconds out of sight (and takes another only in
place of none, of one he cannot see, or of one twice as far off); he
turns to it as fast as the game turns him, draws, and fires only when he
faces it with nothing in the way -- not the player, not a wall -- a real
round with the game's two degrees of wander, one to each of his firing
clips; hidden from him, he goes to where he saw it last; he follows only
when he has nothing to fight, and a standing guard stands again
afterwards. He leaves alone what a scene is playing and what the map
keeps caged. The player's rounds turn him by the game's rule: in a fight
they are strays; with no enemy about, one plainly aimed at him -- or a
second of any kind -- makes him an enemy, who follows nobody
(`shared/HlPeople.hpp`, `test_people`). What he does not do yet: monsters
still have eyes only for the player, so nothing fights him back; turrets
and barnacles he ignores; he says nothing. (A save keeps his grudge and
his drawn pistol with everything else.) People stand on what is under them, brushes included, go where a
lift they stand on goes, and fall when it breaks or is gone; the places
that hurt hurt them as they hurt the player -- which is what c1a1's
falling lift is made of: three scientists on a floor that breaks. The map's entity text, which the console never
read, stays in the stage (`maps/<map>.ent`) instead of in the level.

The loose files the archives are made from stay on the PC, in a **stage**
folder (`--stage <folder>`; by default `<card root>-stage`, beside the
card's): a texture or a sound shared by fifty maps is made once there and
found again by the next run.

`build/convert/hlds-testmap <folder>` writes a small synthetic map -- one room
with a door, a button, a lift and one brush of each drawing class -- that can
be cooked and booted in seconds and contains no game data. It is the quickest
way to look at a rendering or logic change on the console.
