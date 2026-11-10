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
another's name.

A console of another protocol version is told the relay's (at most once a
second to an address), and says which of the two is to be updated. What the two ends say to each other
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
| `--limit NAME=VALUE` | one of the limits below; as often as needed |
| `--quiet` | print nothing while running |

A console that sends nothing for three and a half minutes (`--limit
silent-seconds`) is let go, and with it its room if it was a host. A console
online pings each second, even while a map loads, so this is only for one
that cannot: a Wi-Fi that drops for a while. It costs this much: a console
switched off keeps its place, as the others see it, that long. Whenever the relay lets a console go it tells it why --
its host left or stopped answering, its host sent it away, it said nothing
for too long, or the relay is stopping (Ctrl-C, or `systemctl stop`) -- and
the console goes back to the menu saying so.

A console the relay does not know -- one let go without hearing it, or any
after the relay was started again (it then knows nobody) -- is told so
(`Unknown`, at most once a second to an address), and goes back to the menu
saying the relay was restarted -- unless it was the same relay, and the
console only came from another address (its router gave it another port,
or the network dropped for a moment): it then takes its session up again,
in its room and its place, by a token only it was given, from an address
it has shown is its own. A console that hears nothing from the relay
for ten seconds while it is listening (it pings each second), or whose Wi-Fi
connection goes, goes back to the menu too. One looking for games just looks
again.

## Limits

What a game needs, with room to spare; what is over is let fall (and
counted, and logged once a minute for an address), so that nobody can make
the relay work or send for others. A host sends a frame each 60th of a
second (729 bytes at most) -- and, while it sends a map to a console that has
not got it, up to four pieces of 600 bytes with each: about 300 datagrams
and 190 KB a second. A client sends a frame each 60th (229 bytes at most),
a voice packet each 27 ms at most, and a ping, its room or a list each
second.

| `--limit` | default | |
|---|---|---|
| `hello-per-s` | 10 | Hello and Enter from one address, before its console is known |
| `control-per-s` | 30 | a console's datagrams other than game frames and voice |
| `data-per-s` | 400 | a console's game frames |
| `host-bytes-per-s` | 262144 | a host's game frames, in bytes: each goes to every client of its room |
| `client-bytes-per-s` | 49152 | a client's game frames, in bytes |
| `voice-per-s` | 45 | a console's voice packets |
| `peers-per-ip` | 16 | consoles known at one address at once (several behind one router) |
| `rooms-per-ip` | 4 | rooms open from one address at once |
| `knocks-per-ip` | 8 | consoles of one address waiting at doors at once (one more is told so) |
| `out-bytes-per-s` | 12582912 | bytes the relay sends in all (12 MB/s, about 100 Mbit/s) |
| `empty-room-minutes` | 30 | a room nobody is in or asking to join is closed after this long (0: never) |
| `room-max-minutes` | 0 | any room is closed after this long (0: never) |
| `silent-seconds` | 210 | a console that says nothing this long is let go (3600 at most) |

A room closed for its time sends its host (and any clients) back to the
menu, saying why.

Each rate may go to twice its rate at once (voice: its rate), then keeps to it.
A full room of eight sends about 300 KB/s out in a match, and about 1.4 MB/s
while its host sends a map (the relay passes each piece to every client):
`out-bytes-per-s` is what the server's link (and its provider's traffic
allowance) should bear.

The system holds what comes in while the relay is busy; the relay asks for
4 MB of room, but gets no more than `net.core.rmem_max` allows (often
208 KB): `sysctl -w net.core.rmem_max=4194304` lets it have it.

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
stats peers=12 rooms=3 in=21600 in_bytes=9431200 out=64800 out_bytes=27100000 drop_not_ours=0 other_version=0 resumed=0 drop_too_many=0 drop_unknown=0 drop_bad_cookie=0 drop_rate_hello=0 drop_rate_control=0 drop_rate_data=0 drop_rate_voice=0 drop_cap_peers=0 drop_cap_rooms=0 drop_cap_knocks=0 drop_out_budget=0 send_failed=0 full_rounds=0 (last 60 s)
```

| | |
|---|---|
| `peers`, `rooms` | consoles and rooms known now |
| `in`, `in_bytes` | datagrams come in that minute, and their bytes |
| `out`, `out_bytes` | datagrams sent, and their bytes |
| `drop_not_ours` | datagrams that were not this relay's protocol |
| `other_version` | datagrams of this protocol but another version: a game, or a relay, to be updated (each address is told the relay's version, at most once a second) |
| `resumed` | consoles that took their session up again from another address (a router gave them another port, or they came back after a drop) |
| `drop_too_many` | consoles coming in while the relay knew as many as it may (1024) |
| `drop_unknown` | datagrams from no console the relay knows, or in another session than the one there: a console it let go, a relay started again, or someone making them up |
| `drop_bad_cookie` | consoles coming in with a cookie the relay did not make for that address, or one too old |
| `drop_rate_*` | over a rate (see Limits): `hello`, coming in; `control`; `data`, game frames; `voice` |
| `drop_cap_*` | over what one address may have at once: `peers`, `rooms`, `knocks` |
| `drop_out_budget` | not sent: the relay had sent as much as `out-bytes-per-s` lets it |
| `send_failed` | datagrams the system would not send |
| `full_rounds` | times it had more come at once than it reads in one go (256): under a flood, this climbs |

## Tests

`ctest` runs two:

- `test_relay`: every message, every limit, every way a console goes.
- `fuzz_relay`: a million datagrams, mostly wrong ones. Consoles play as they
  should, the same datagrams come with bits flipped, cut short or run on,
  strangers say anything in sessions they make up, and noise. After each,
  the relay must not:
  - answer one it does not know anywhere but where it is;
  - answer one it does not know with more than it was sent;
  - send anything but its own datagrams;
  - let what it keeps disagree with itself (`Relay::consistent`).

  It is the same each run, and CI runs it under AddressSanitizer and
  UndefinedBehaviorSanitizer.

For longer runs, libFuzzer (clang):

```sh
CXX=clang++ cmake -S tools/relay -B build/relay-fuzz -DHLDS_RELAY_LIBFUZZER=ON -DHLDS_BUILD_TESTS=OFF
cmake --build build/relay-fuzz --target relay_libfuzzer
mkdir -p build/relay-fuzz/corpus
build/relay-fuzz/relay_libfuzzer build/relay-fuzz/corpus -max_total_time=600
```

## Load test

`hlds-relay-loadtest` plays rooms of make-believe consoles through a relay
over real UDP, as the game does. Each console has an address of its own on
the loopback (`127.1.<room>.<n>`), so the limits for one address do not get
in the way. In each room the host sends a frame of 720 bytes each 60th of a
second and each client one of 220, with a ping each second. With `--map`
the host also sends four map pieces of 620 bytes with each frame, as it
does sending a map. Each frame says when it was sent and its number: the
tool reports what was lost, how late what came was, and the relay's CPU.

```sh
build/relay/hlds-relay --port 27500 &
build/relay/hlds-relay-loadtest --relay 127.0.0.1:27500 --relay-pid $! --rooms 30 --players 8 --seconds 30
build/relay/hlds-relay-loadtest --spawn build/relay/hlds-relay --rooms 8 --map     # (it starts its own relay)
build/relay/hlds-relay-loadtest --spawn build/relay/hlds-relay --rooms 8 --flood 20000
```

| option | |
|---|---|
| `--relay HOST:PORT` | where the relay is (127.0.0.1:27500) |
| `--spawn PATH` | start this relay on a port of its own, and stop it after |
| `--rooms N`, `--players P` | how many rooms (up to 250), and consoles in each (2 to 8) |
| `--seconds S` | how long, after three seconds to come in and join |
| `--map` | hosts send a map as they play |
| `--flood PPS` | strangers' datagrams a second on top (pings in made-up sessions, Hellos, noise) |
| `--relay-pid PID` | the relay's process, to read its CPU |
| `--most-loss PERCENT` | fail above this loss (5) |

`ctest` runs a short one (two full rooms, a map being sent, three seconds).

Measured 2026-11-10, relay and test on one laptop (Intel Core i7-1165G7, loopback):

| load | out | lost | late, 99 in 100 | relay CPU |
|---|---|---|---|---|
| 30 rooms of 8, a match | 11.8 MB/s | 0 | 3.4 ms | 29% of a core |
| 30 rooms of 8, every host sending a map | 42.7 MB/s | 0 (\*) | 7.7 ms | 52% |
| 8 rooms of 8, and 20,000 strangers' datagrams a second | 3.1 MB/s | 0 | 3.9 ms | 17% |

(\*) With `--limit out-bytes-per-s=200000000`. At the default of 12 MB/s,
the relay sends no more than that and lets the rest fall: about eight rooms
can send a map at once.

A small VM is slower than this PC, and its link and its provider's traffic
allowance decide more than its CPU: run the load test against it from
outside before opening it to players.

## A bad network, by hand (`tc netem`)

Linux can make a network interface slow and lossy on purpose (as root).
To try the relay and the consoles on a bad Internet connection, at home:

```sh
# On the PC running the relay, on the interface the DSi reaches it by
# (ip link shows the names: wlan0, enp3s0...). What goes OUT of it: the
# relay's datagrams to the consoles.
sudo tc qdisc add dev wlan0 root netem delay 80ms 20ms distribution normal loss 2%

# Change it as it runs (worse):
sudo tc qdisc change dev wlan0 root netem delay 150ms 40ms loss 5% duplicate 1% reorder 5%

# And ALWAYS take it off afterwards (it stays until then, for everything):
sudo tc qdisc del dev wlan0 root
```

- `delay 80ms 20ms distribution normal`: each datagram 80 ms late, give or
  take 20, as over a long way.
- `loss 2%`: 2 in 100 lost.
- `duplicate`, `reorder`: some sent twice, some overtaken.

It only touches what goes out of that interface. To do the same to what
comes in as well, run it on the router, or on both ends.

With the load test, all on the one PC: the loopback (`dev lo`) carries
both ways, so each figure counts twice, and every program using the
loopback is slowed too while it is on:

```sh
sudo tc qdisc add dev lo root netem delay 40ms 10ms loss 1%
build/relay/hlds-relay-loadtest --spawn build/relay/hlds-relay --rooms 4 --most-loss 10
sudo tc qdisc del dev lo root
```

What to look for: on the consoles, a match that goes on (with some
stutter) at 150 ms and 5% loss; on the relay, no `went quiet` for a console
that is still there.

## On a server

A small Linux server is enough: Oracle Cloud's free one (ARM, aarch64) or an
OVH VPS (x86_64), on Ubuntu 22.04, 24.04 or 26.04, or Debian 12 or 13.
`deploy/install.sh` sets it up. Run it as root, on the server:

```sh
# (On your PC: the release's relay for the server's machine, and its sums.)
scp hlds-relay-linux-aarch64 SHA256SUMS.txt tools/relay/deploy/* you@server:~/relay/
# (On the server.)
cd ~/relay && sudo ./install.sh --binary hlds-relay-linux-aarch64 --sums SHA256SUMS.txt
# ... or straight from a GitHub release:
sudo ./install.sh --release v0.4.0
```

`--dry-run` says what it would do, and does nothing. Run again, it does no
harm, and keeps `/etc/hlds-relay.conf` as you left it.

What it does:

1. **Checks the relay.** Its SHA-256 must be the release's (`--sums
   SHA256SUMS.txt`, or `--sha256 HASH`), or nothing is installed. Then it puts
   it at `/usr/local/bin/hlds-relay`.
2. **The service** (see "The service" below): the unit, the options file
   (`--port`; 27500 if not given), the journal kept a week, and
   `net.core.rmem_max` raised so the relay gets the 4 MB of room it asks for.
   It enables and starts the service, and restarts it when the program
   changed.
3. **The firewall: nothing in but SSH and the relay's UDP port.** With ufw:
   deny in, allow out, allow SSH (the port sshd listens on, or
   `--ssh-port`), allow UDP 27500. On Oracle Cloud's images, which come with
   iptables rules of their own (ufw would fight them), it adds one rule for
   the relay's port in `/etc/iptables/rules.v4`, just before the rule that
   turns the rest away. It keeps the rules before as
   `rules.v4.before-hlds-relay`. `--no-firewall` leaves it alone.
4. **Security updates applied by themselves:** `unattended-upgrades`,
   switched on daily.
5. **SSH by key only:** passwords off, root by key only
   (`/etc/ssh/sshd_config.d/10-hlds-relay.conf`). It does this **only if**
   the account you ran it from (with sudo) has a key in its
   `~/.ssh/authorized_keys`, so it can't shut you out of your own server.
   It checks the result with `sshd -t` first, and takes it back if sshd
   would not take it. Keep your session open until a new one comes in by
   key. `--no-ssh-keys-only` leaves SSH alone.

What it cannot do, outside the machine:

- **Oracle Cloud:** the virtual network has a firewall of its own. In the
  console: Networking, your VCN, its subnet's **security list** (or the
  instance's network security group). Add an ingress rule: source
  `0.0.0.0/0`, protocol **UDP**, destination port **27500**. Without it,
  nothing reaches the server, whatever the server's own firewall says.
- **OVH Public Cloud** (instances): the instance's **security group**. Add
  an ingress rule for UDP 27500.
- **OVH VPS or dedicated server:** the **Network Firewall** is on the
  server's IP, in the control panel. If it is on, add a rule letting UDP
  27500 in.
- **The server's address** goes in each console's `server.ini` (`host =`).
  Give it a name, if you have a domain, so that it can change.

To take it all away:

```sh
sudo systemctl disable --now hlds-relay
sudo rm /usr/local/bin/hlds-relay /etc/systemd/system/hlds-relay.service /etc/hlds-relay.conf \
        /etc/systemd/journald@hlds-relay.conf /etc/sysctl.d/90-hlds-relay.conf
sudo ufw delete allow 27500/udp           # (or, on Oracle: the rule in /etc/iptables/rules.v4)
sudo systemctl daemon-reload
```

(The SSH and update settings are good to keep.)

## The service

On a server, systemd runs the relay: it starts it with the machine, starts
it again if it stops, keeps what it prints, and shuts it in, so that even a
fault in it would reach nothing on the machine but its own UDP port. Three
files in `deploy/` do it (`install.sh` puts them in place):

| file | installed as | what it is |
|---|---|---|
| `hlds-relay.service` | `/etc/systemd/system/hlds-relay.service` | the service: how the relay is run |
| `hlds-relay.conf.example` | `/etc/hlds-relay.conf` | its options (`HLDS_RELAY_ARGS=...`) |
| `journald@hlds-relay.conf` | `/etc/systemd/journald@hlds-relay.conf` | how long what it prints is kept |

Day to day:

```sh
sudo systemctl status hlds-relay            # running? since when? its last lines
sudo systemctl restart hlds-relay           # after changing /etc/hlds-relay.conf
journalctl --namespace=hlds-relay -f        # what it says, as it says it (Ctrl-C to stop)
journalctl --namespace=hlds-relay --since "1 hour ago"
systemd-analyze security hlds-relay         # how shut in it is (lower is better)
```

### The unit, line by line

A unit file is in sections (`[Unit]`, `[Service]`, `[Install]`), each a list
of `Name=value` settings. `man systemd.service`, `man systemd.exec` and
`man systemd.resource-control` say everything; here is what each line of
this one does, and why.

**`[Unit]`**: what the service is, and when it starts.

| line | what it does |
|---|---|
| `Description=` | the name `systemctl status` shows |
| `Documentation=` | where to read about it (this file) |
| `Wants=network-online.target`, `After=network-online.target` | start once the machine's network is up, not before (the relay listens on it) |
| `StartLimitIntervalSec=60`, `StartLimitBurst=5` | if it fails five times within a minute, stop trying (something is wrong: see `systemctl status`), rather than restart it for ever |

**`[Service]`**: how it runs.

| line | what it does |
|---|---|
| `Type=simple` | the program is the service: running while it runs |
| `EnvironmentFile=-/etc/hlds-relay.conf` | read the options from this file (the `-`: no file is no error) |
| `ExecStart=/usr/local/bin/hlds-relay $HLDS_RELAY_ARGS` | what is run, with the options from that file |
| `Restart=on-failure`, `RestartSec=2s` | if it stops of itself (a crash), start it again two seconds later; not if it is stopped on purpose |
| `TimeoutStopSec=5s` | asked to stop (SIGTERM), it tells every console and ends at once; if not within five seconds, it is killed |

*Who it runs as:*

| line | what it does |
|---|---|
| `DynamicUser=yes` | it runs as a user made up for it as it starts, and gone when it stops: it owns no file anywhere, and is not root |
| `PrivateUsers=yes` | ... and that user sees no other user of the machine |

*What it may see and touch.* The relay reads no file and writes none, so
all of it is shut away:

| line | what it does |
|---|---|
| `ProtectSystem=strict` | the whole file system is read-only to it |
| `ProtectHome=yes` | `/home`, `/root` and `/run/user` are not there for it |
| `PrivateTmp=yes` | a `/tmp` of its own, empty |
| `PrivateDevices=yes` | no devices (disks, terminals...) but the harmless ones (`/dev/null`, `/dev/urandom`...) |
| `PrivateIPC=yes` | its own shared memory and message queues: none of the machine's |
| `ProtectProc=invisible`, `ProcSubset=pid` | in `/proc` it sees its own process only, and nothing of the kernel's |
| `ProtectKernelTunables=yes` | it cannot change the kernel's settings (`/proc/sys`, `/sys`) |
| `ProtectKernelModules=yes` | ... nor load a kernel module |
| `ProtectKernelLogs=yes` | ... nor read the kernel's log |
| `ProtectControlGroups=yes` | ... nor change the control groups (how the machine's resources are shared) |
| `ProtectClock=yes` | ... nor set the clock |
| `ProtectHostname=yes` | ... nor the machine's name |
| `UMask=0077` | anything it did create would be its own alone |
| `KeyringMode=private` | the kernel's key store: none of the machine's keys |

*What it may do:*

| line | what it does |
|---|---|
| `NoNewPrivileges=yes` | it can never gain more rights than it starts with (no `sudo`, no set-user-ID programs) |
| `CapabilityBoundingSet=`, `AmbientCapabilities=` | empty: none of root's special powers, not even one. (Port 27500 is above 1024: no power is needed to listen on it) |
| `RestrictAddressFamilies=AF_INET AF_UNIX` | it can open IPv4 sockets (its UDP port) and local ones, nothing else (no raw packets, no IPv6, no netlink) |
| `RestrictNamespaces=yes` | it cannot make containers of its own |
| `RestrictRealtime=yes` | it cannot take the CPU from everything else |
| `RestrictSUIDSGID=yes` | it cannot make set-user-ID files |
| `LockPersonality=yes` | it cannot ask the kernel to behave as another system |
| `MemoryDenyWriteExecute=yes` | no memory it can both write and run: code smuggled in a datagram could not be run |
| `SystemCallArchitectures=native` | only the machine's own kind of system calls |
| `SystemCallFilter=@system-service` | only the system calls an ordinary service uses (systemd's list) |
| `SystemCallFilter=~@privileged @resources` | ... less those that need root, and those that change limits or priorities |
| `SystemCallErrorNumber=EPERM` | a call it may not make fails ("not permitted") rather than killing it |

*How much it may take.* It needs about 5 MB:

| line | what it does |
|---|---|
| `MemoryMax=64M` | never more than 64 MB of memory (else it is stopped, and started again) |
| `TasksMax=4` | never more than four threads (it uses one) |
| `LimitNOFILE=64` | never more than 64 open files and sockets (it uses one) |

*Where what it prints goes:*

| line | what it does |
|---|---|
| `LogNamespace=hlds-relay` | a journal of its own, apart from the machine's, kept as `journald@hlds-relay.conf` says. Read it with `journalctl --namespace=hlds-relay` (plain `journalctl -u hlds-relay` does not show it) |
| `SyslogIdentifier=hlds-relay` | the name its lines carry |

**`[Install]`**

| line | what it does |
|---|---|
| `WantedBy=multi-user.target` | `systemctl enable hlds-relay` starts it with the machine, in its ordinary running state |

`systemd-analyze security hlds-relay` rates it **1.1, "OK"** (0 is shut in
altogether, 10 not at all). What is left counts as exposure only because it
is a server: it uses the network, takes datagrams from any address, and
opens Internet sockets.

### How long what it prints is kept

`journald@hlds-relay.conf` keeps the relay's journal **a week**
(`MaxRetentionSec=7day`), in at most **50 MB** (`SystemMaxUse=50M`), on
disk (`Storage=persistent`). Older lines are deleted by themselves. What is
in it: consoles coming and going and rooms, by the relay's own numbers and
addresses cut to their first three numbers (`--log-ips`), and a line of
counts a minute; never a player's or room's name. To keep it shorter, lower
`MaxRetentionSec`, then `sudo systemctl restart systemd-journald@hlds-relay`.
To delete it all now: `sudo journalctl --namespace=hlds-relay --rotate --vacuum-time=1s`.

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
