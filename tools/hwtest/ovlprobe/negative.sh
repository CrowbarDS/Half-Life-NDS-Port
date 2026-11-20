#!/usr/bin/env bash
# The builds tools/ovl.py (or the link) must refuse: each one built on its
# own (NEG=..., the Makefile), and each must fail, saying why.
#   ungated  the program calls into an overlay without its gate   (R1)
#   ctor     a static constructor in an overlay                   (R0)
#   big      an overlay larger than the one its region is sized by (link)
#   notable  a program that reads the ROM's overlay table, in a ROM made
#            without one (romcheck)
set -u
cd "$(dirname "$0")"
export BLOCKSDS="${HLDS_BLOCKSDS:-/opt/blocksds/hl-known-good}"
fail=0
check() {
    local neg=$1 want=$2 out
    out=$(make -j2 NEG="$neg" ${3:+OVL_MODE=$3} 2>&1)
    if [ $? -eq 0 ]; then
        echo "negative.sh: NEG=$neg was built: it must not be"; fail=1
    elif ! grep -q -- "$want" <<<"$out"; then
        echo "negative.sh: NEG=$neg failed, but not with \"$want\":"; echo "$out" | tail -5; fail=1
    else
        echo "negative.sh: NEG=$neg refused: $(grep -m1 -- "$want" <<<"$out" | sed 's/^.*ovl.py check: //')"
    fi
}
check ungated 'R1 '
check ctor 'R0 one: .init_array'
check big 'overlay one is larger than two'
check notable 'overlay table is 0 bytes' rom
exit $fail
