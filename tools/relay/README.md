# hlds-relay

The relay that consoles playing online meet through. A host opens a room on
it, the others are given the list of rooms and ask to join one -- the host is
asked, by name, whether to let each in, and one not answered within 30
seconds is turned away -- and from then on the
relay passes the host's frames to every client of the room and each client's
to the host, and voice packets (when the host allows voice) to everyone else
in the room. It never reads a game frame: the lobby and the match are the
same ones played over the local wireless. What the two ends say to each other
is in `shared/HlRelay.hpp`.

Online play is for a DSi (in DSi mode), on one of the console's saved Wi-Fi
connections.

## Build and run

```sh
cmake -S tools/relay -B build/relay
cmake --build build/relay -j2
ctest --test-dir build/relay
build/relay/hlds-relay --port 27500
```

| option | |
|---|---|
| `--port N` | the UDP port to listen on (27500 if not given) |
| `--bind ADDRESS` | the local IPv4 address to listen on (all of them if not given) |
| `--quiet` | print nothing while running |

It prints a line when a room opens or closes and when a console joins or
leaves. A console that sends nothing for 45 seconds is let go, and with it
its room if it was a host.

## The console's side

The card says where the relay is, in `_nds/hl_nds/data/server.ini` (see
`server.ini.example`):

```ini
host = relay.example.org
port = 27500
```

Without the file, MULTIPLAYER's ONLINE tile is greyed out and says why. The
converter's installer never writes or removes it.

## Testing on one PC

Run the relay, then the ROM in melonDS's DSi mode with its own network (not
"direct mode"): melonDS reaches the PC at `10.0.2.2`, so the card's
`server.ini` says `host = 10.0.2.2`. Two melonDS windows can each host or
join.
