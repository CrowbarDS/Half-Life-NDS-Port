# hlds-relay

The relay that consoles playing online meet through. A host opens a room on
it, the others are given the list of rooms and ask to join one -- the host is
asked, by name, whether to let each in, and one not answered within 30
seconds is turned away -- and from then on the
relay passes the host's frames to every client of the room and each client's
to the host, and voice packets (when the host allows voice) to everyone else
in the room. It never reads a game frame: the lobby and the match are the
same ones played over the local wireless.

A console is known to the relay only once it has shown that it gets what is
sent to its address: it says Hello, is given a cookie made from its address
under a secret the relay makes at random each time it starts, and sends it
back. Until then the relay keeps nothing for it, answers nothing but that,
and never with more than it was sent -- so it cannot be made to fill its
tables, or to send to an address that did not ask, by datagrams sent in
another's name. What the two ends say to each other
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
| `--log-ips trunc\|hash\|full` | how consoles' addresses are logged (`trunc` if not given; see below) |
| `--quiet` | print nothing while running |

A console that sends nothing for 45 seconds is let go, and with it its room
if it was a host.

## What it logs

A line when a console comes or goes, when a room opens or closes, and when a
console joins or leaves one. A console is named by a number of the relay's
own (`#12`), given when it comes, and its address. No player's or room's name
is logged.

An address says who someone is, near enough, so it is logged no more than
`--log-ips` asks:

| `--log-ips` | logged as | |
|---|---|---|
| `trunc` (default) | `192.0.2.x` | the first three numbers |
| `hash` | `h:4bf440` | a hash under a secret made at random each day (UTC), never written anywhere: the same address gives the same hash all that day, and a different one the next |
| `full` | `192.0.2.1:5000` | address and port: to find a fault, not on a public relay |

Once a minute it logs a line of counts:

```
stats peers=12 rooms=3 in=21600 in_bytes=9431200 out=64800 out_bytes=27100000 drop_not_ours=0 drop_too_many=0 drop_unknown=0 drop_bad_cookie=0 send_failed=0 full_rounds=0 (last 60 s)
```

| | |
|---|---|
| `peers`, `rooms` | consoles and rooms known now |
| `in`, `in_bytes` | datagrams come in that minute, and their bytes |
| `out`, `out_bytes` | datagrams sent, and their bytes |
| `drop_not_ours` | datagrams that were not this relay's protocol, or were of another version |
| `drop_too_many` | consoles coming in while the relay knew as many as it may (1024) |
| `drop_unknown` | datagrams from no console the relay knows, or in another session than the one there: a console it let go, a relay started again, or someone making them up |
| `drop_bad_cookie` | consoles coming in with a cookie the relay did not make for that address, or one too old |
| `send_failed` | datagrams the system would not send |
| `full_rounds` | times it had more come at once than it reads in one go (256): under a flood, this climbs |

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
