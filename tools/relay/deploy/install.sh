#!/usr/bin/env bash
# Puts the relay on a fresh Ubuntu or Debian server, shut in and kept up to
# date: the program (its SHA-256 checked), its systemd service and journal,
# a firewall that lets in SSH and the relay's UDP port alone, the system's
# security updates applied by themselves, and SSH by key only.
#
#   sudo ./install.sh --binary hlds-relay-linux-aarch64 --sums SHA256SUMS.txt
#   sudo ./install.sh --release v0.4.0                     # (fetched from GitHub)
#   ./install.sh --dry-run ...                             # says what it would do, does nothing
#
# Run again, it does no harm: what is there already is left as it is (the
# options in /etc/hlds-relay.conf above all), what has changed is put right.
# tools/relay/README.md ("On a server") says the whole of it.

set -euo pipefail

REPO="CrowbarDS/Half-Life-NDS-Port"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

binary=""
sums=""
sha=""
release=""
port=27500
sshPort=""
dryRun=0
firewall=1
sshKeys=1

usage() {
    cat <<'EOF'
usage: install.sh (--binary FILE (--sums SHA256SUMS.txt | --sha256 HASH) | --release VERSION)
                  [--port N] [--ssh-port N] [--no-firewall] [--no-ssh-keys-only] [--dry-run]

  --binary FILE       the relay to install (hlds-relay-linux-<arch> of a release)
  --sums FILE         the release's SHA256SUMS.txt: the binary must be in it, and match
  --sha256 HASH       ... or its SHA-256, given here
  --release VERSION   fetch both from the project's GitHub release (v0.4.0, say)
  --port N            the relay's UDP port (27500)
  --ssh-port N        the SSH port the firewall keeps open (sshd's own, or 22)
  --no-firewall       leave the firewall alone
  --no-ssh-keys-only  leave SSH's own settings alone
  --dry-run           say what would be done, and do nothing
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) binary="${2:?}"; shift 2 ;;
    --sums) sums="${2:?}"; shift 2 ;;
    --sha256) sha="${2:?}"; shift 2 ;;
    --release) release="${2:?}"; shift 2 ;;
    --port) port="${2:?}"; shift 2 ;;
    --ssh-port) sshPort="${2:?}"; shift 2 ;;
    --no-firewall) firewall=0; shift ;;
    --no-ssh-keys-only) sshKeys=0; shift ;;
    --dry-run) dryRun=1; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
    esac
done

say() { printf '\n== %s\n' "$*"; }
note() { printf '   %s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

# What changes the machine goes through these: in a dry run, only said.
run() {
    if [ "$dryRun" = 1 ]; then
        printf '   would run: %s\n' "$*"
    else
        "$@"
    fi
}
# A file written whole (from what comes in), with its mode; left alone if it
# is so already.
put() {
    local file="$1" mode="$2" content
    content="$(cat)"
    if [ -f "$file" ] && [ "$(cat "$file")" = "$content" ]; then
        note "$file: as it should be"
        return 0
    fi
    if [ "$dryRun" = 1 ]; then
        printf '   would write %s (%s)\n' "$file" "$mode"
        return 0
    fi
    mkdir -p "$(dirname "$file")"
    printf '%s\n' "$content" >"$file.new"
    chmod "$mode" "$file.new"
    mv "$file.new" "$file"
    note "$file: written"
}

case "$port" in '' | *[!0-9]*) die "not a port: $port" ;; esac
# (The SSH port the firewall keeps open: the one sshd listens on, unless
# said -- a firewall that shut it would shut the operator out.)
if [ -z "$sshPort" ]; then
    sshPort="$( (sshd -T 2>/dev/null || true) | awk '$1 == "port" { print $2; exit }')"
    sshPort="${sshPort:-22}"
fi
case "$sshPort" in '' | *[!0-9]*) die "not a port: $sshPort" ;; esac
[ "$port" -ge 1024 ] && [ "$port" -le 65535 ] || die "the relay's port must be 1024..65535 (it has no power to use one below)"

# ------------------------------------------------------------------ the machine
say "The machine"
[ -r /etc/os-release ] || die "no /etc/os-release: not a system this knows"
# shellcheck disable=SC1091
. /etc/os-release
case "${ID:-}:${VERSION_ID:-}" in
ubuntu:22.04 | ubuntu:24.04 | ubuntu:26.04 | debian:12 | debian:13) note "$PRETTY_NAME" ;;
*) die "${PRETTY_NAME:-this system} is not one this knows (Ubuntu 22.04, 24.04 or 26.04, Debian 12 or 13)" ;;
esac
case "$(uname -m)" in
x86_64) arch=x86_64 ;;
aarch64 | arm64) arch=aarch64 ;;
*) die "a $(uname -m) machine: there is no relay built for it" ;;
esac
note "$arch"
if [ "$dryRun" = 0 ] && [ "$(id -u)" != 0 ]; then
    die "run it as root (sudo ./install.sh ...), or with --dry-run"
fi
[ -d /run/systemd/system ] || [ "$dryRun" = 1 ] || die "systemd is not running here: the relay is run by it"
command -v sha256sum >/dev/null || die "no sha256sum"

# ------------------------------------------------------------------ the program
say "The relay"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [ -n "$release" ]; then
    command -v curl >/dev/null || die "no curl, to fetch the release (apt-get install curl)"
    base="https://github.com/$REPO/releases/download/$release"
    note "fetching hlds-relay-linux-$arch and SHA256SUMS.txt of $release"
    curl -fsSL --proto '=https' -o "$work/hlds-relay-linux-$arch" "$base/hlds-relay-linux-$arch" ||
        die "could not fetch $base/hlds-relay-linux-$arch"
    curl -fsSL --proto '=https' -o "$work/SHA256SUMS.txt" "$base/SHA256SUMS.txt" ||
        die "could not fetch $base/SHA256SUMS.txt"
    binary="$work/hlds-relay-linux-$arch"
    sums="$work/SHA256SUMS.txt"
fi
[ -n "$binary" ] || { usage >&2; die "say which relay: --binary FILE, or --release VERSION"; }
[ -f "$binary" ] || die "no such file: $binary"
got="$(sha256sum "$binary" | cut -d' ' -f1)"
if [ -n "$sums" ]; then
    [ -f "$sums" ] || die "no such file: $sums"
    want="$(awk -v n="$(basename "$binary")" '$2 == n || $2 == "*" n { print $1 }' "$sums" | head -n1)"
    [ -n "$want" ] || die "$(basename "$binary") is not in $sums"
elif [ -n "$sha" ]; then
    want="$sha"
else
    die "nothing to check $binary against: give --sums SHA256SUMS.txt or --sha256 HASH"
fi
[ "$got" = "$want" ] || die "$binary is not the one released: its SHA-256 is $got, not $want"
note "SHA-256 checked: $got"
case "$(head -c 20 "$binary" | od -An -tx1 | tr -d ' \n')" in
7f454c4602*) ;;
*) die "$binary is not a 64-bit Linux program" ;;
esac
if [ -f /usr/local/bin/hlds-relay ] && cmp -s "$binary" /usr/local/bin/hlds-relay; then
    note "/usr/local/bin/hlds-relay: the same already"
    newBinary=0
else
    run install -m 0755 -o root -g root "$binary" /usr/local/bin/hlds-relay
    newBinary=1
fi

# ------------------------------------------------------------------ the service
say "Its service and journal"
put /etc/systemd/system/hlds-relay.service 0644 <"$HERE/hlds-relay.service"
put /etc/systemd/journald@hlds-relay.conf 0644 <"$HERE/journald@hlds-relay.conf"
if [ -f /etc/hlds-relay.conf ]; then
    note "/etc/hlds-relay.conf: kept as it is (its options are the operator's)"
    grep -q -- "--port $port\b" /etc/hlds-relay.conf ||
        note "NOTE: it does not say --port $port: the firewall below opens $port"
else
    sed "s/--port 27500/--port $port/" "$HERE/hlds-relay.conf.example" | put /etc/hlds-relay.conf 0644
fi
# (Room for a burst of datagrams: the relay asks for 4 MB.)
put /etc/sysctl.d/90-hlds-relay.conf 0644 <<'EOF'
# hlds-relay: room for a burst of datagrams (it asks for 4 MB).
net.core.rmem_max = 4194304
EOF
run sysctl -q --system || note "sysctl would not take it all: the relay runs, with less room for bursts"
run systemctl daemon-reload
run systemctl enable hlds-relay.service
if [ "$newBinary" = 1 ] || ! systemctl is-active -q hlds-relay.service 2>/dev/null; then
    run systemctl restart hlds-relay.service
else
    note "running, the same program: not restarted"
fi

# (What is to be installed, from lists brought up to date first, once.)
listsFresh=0
aptInstall() {
    if [ "$listsFresh" = 0 ]; then
        run apt-get update -q
        listsFresh=1
    fi
    run env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "$@"
}

# ------------------------------------------------------------------ the firewall
say "The firewall"
oracle=0
if [ -f /etc/iptables/rules.v4 ] && grep -q -- '-j REJECT' /etc/iptables/rules.v4 && grep -q 'icmp-host-prohibited' /etc/iptables/rules.v4; then
    oracle=1
fi
if [ "$firewall" = 0 ]; then
    note "left alone (--no-firewall): open UDP $port yourself"
elif [ "$oracle" = 1 ]; then
    # Oracle Cloud's images: their own iptables rules, which let in SSH and
    # turn the rest away (ufw would fight them). The relay's port, let in
    # before that.
    note "Oracle Cloud's iptables rules (/etc/iptables/rules.v4)"
    rule="-A INPUT -p udp -m state --state NEW -m udp --dport $port -j ACCEPT"
    if grep -qF -- "$rule" /etc/iptables/rules.v4; then
        note "UDP $port: let in already"
    elif [ "$dryRun" = 1 ]; then
        note "would let UDP $port in, before the first REJECT of INPUT, and load the rules"
    else
        cp /etc/iptables/rules.v4 "/etc/iptables/rules.v4.before-hlds-relay"
        awk -v r="$rule" '!done && /^-A INPUT .*-j REJECT/ { print r; done = 1 } { print }' \
            /etc/iptables/rules.v4.before-hlds-relay >/etc/iptables/rules.v4
        grep -qF -- "$rule" /etc/iptables/rules.v4 || die "no REJECT in INPUT to put the rule before: rules.v4 left as it was"
        iptables-restore </etc/iptables/rules.v4
        note "UDP $port let in (the rules before are in rules.v4.before-hlds-relay)"
    fi
else
    # ufw: nothing in but SSH and the relay; everything out.
    command -v ufw >/dev/null || aptInstall ufw
    note "SSH on port $sshPort: kept open"
    run ufw default deny incoming
    run ufw default allow outgoing
    run ufw allow "$sshPort/tcp" comment 'SSH'
    run ufw allow "$port/udp" comment 'hlds-relay'
    run ufw --force enable
fi
note "AND the cloud's own firewall, outside the machine, must let UDP $port in:"
note "  Oracle Cloud: the VCN's security list (or the instance's network security group),"
note "    an ingress rule: source 0.0.0.0/0, protocol UDP, destination port $port"
note "  OVH Public Cloud (instances): the instance's security group, an ingress rule for UDP $port"
note "  OVH VPS or dedicated server: the Network Firewall on its IP, if it is on, a rule for UDP $port"

# ------------------------------------------------------------------ updates
say "Security updates, by themselves"
if dpkg -s unattended-upgrades >/dev/null 2>&1; then
    note "unattended-upgrades: there"
else
    aptInstall unattended-upgrades
fi
put /etc/apt/apt.conf.d/20auto-upgrades 0644 <<'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF

# ------------------------------------------------------------------ SSH
say "SSH: by key only"
owner="${SUDO_USER:-root}"
home="$(getent passwd "$owner" | cut -d: -f6)"
if [ "$sshKeys" = 0 ]; then
    note "left alone (--no-ssh-keys-only)"
elif [ -z "$home" ] || [ ! -s "$home/.ssh/authorized_keys" ] || ! grep -qE '^(ssh-|ecdsa-|sk-)' "$home/.ssh/authorized_keys"; then
    # (Turning passwords off with no key to come in by would shut the
    # operator out of their own server.)
    note "NOT DONE: $owner has no key in $home/.ssh/authorized_keys -- turning passwords off"
    note "now would shut you out. Put your public key there, then run this again."
else
    put /etc/ssh/sshd_config.d/10-hlds-relay.conf 0644 <<'EOF'
# hlds-relay's install.sh: SSH by key only. (Read before the other files
# here: in sshd, the first value given wins.)
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin prohibit-password
EOF
    if [ "$dryRun" = 0 ]; then
        if sshd -t; then
            systemctl reload ssh.service 2>/dev/null || systemctl reload sshd.service
            note "passwords off; $owner comes in by key (keep this session open until you have checked a new one does)"
        else
            rm -f /etc/ssh/sshd_config.d/10-hlds-relay.conf
            die "sshd would not take it (sshd -t): taken back, SSH left as it was"
        fi
    fi
fi

# ------------------------------------------------------------------ done
say "Done"
if [ "$dryRun" = 1 ]; then
    note "(a dry run: nothing was changed)"
else
    systemctl --no-pager --lines=0 status hlds-relay.service || true
    note "$(systemd-analyze security hlds-relay.service 2>/dev/null | tail -n1)"
fi
note "What it says:      journalctl --namespace=hlds-relay -f"
note "Its options:       /etc/hlds-relay.conf, then: systemctl restart hlds-relay"
note "On the console:    host = this server's address, port = $port (server.ini)"
