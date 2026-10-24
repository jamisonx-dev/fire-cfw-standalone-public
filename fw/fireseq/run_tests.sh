#!/bin/sh
# Every host test of the Fire firmware, one summary line each; exit 1 if any fails. Run on the Pi in the tree being
# released (gcc, python3 + numpy/mido/Pillow there). Builds into a temp dir, leaves nothing behind.
#   sh run_tests.sh          the suites (~1 min)
#   sh run_tests.sh --mut    + the mutation scripts (several minutes)
cd "$(dirname "$0")" || exit 1
T=$(mktemp -d "$HOME/.fwtest.XXXXXX") || exit 1
trap 'rm -rf "$T"; rm -f scr_*.raw' EXIT
fail=0
run() {   # run <name> <command...>: one line, PASS / FAIL + the suite's own last line
    name=$1; shift
    out=$("$@" 2>&1); rc=$?
    printf '%-14s %s  %s\n' "$name" "$([ $rc -eq 0 ] && echo PASS || echo FAIL)" "$(echo "$out" | tail -1)"
    [ $rc -eq 0 ] || fail=1
}
c() {     # c <name> <gcc args...>: build a host test, then run it
    name=$1; shift
    if gcc "$@" -lm -o "$T/$name" 2>"$T/$name.err"; then run "$name" "$T/$name"
    else printf '%-14s FAIL  (does not build: %s)\n' "$name" "$(head -1 "$T/$name.err")"; fail=1; fi
}
W="-O1 -Wall -Wextra -I."
c ctl_test     $W ctl_test.c ctl.c lights.c
c crash_test   $W crash_test.c crash.c
c lights_test  $W lights_test.c lights.c
c screens_test $W screens_test.c screens.c draw.c
c steps_test   -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error steps_test.c steps.c
c play_test    $W play_test.c play.c
c store2_test  -O2 -Wall store2_test.c store2.c store.c
# the owner's REAL save areas (read off the Fire): the oldest formats, and his patterns as v0.28 saved them (v2 records).
# They are personal dumps, not in this repo: without them this step is skipped (read one off your own Fire with
# dfu-util -a 0 -s 0x08020000:131072 -U <file>, in update mode, and put it there to run it).
if gcc -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error convert_test.c steps.c store2.c -o "$T/convert_test" 2>"$T/ct.err"; then
    for a in sector5_before_v010.bin sector5_v028b_20261023.bin; do
        if [ -f "$HOME/fire_fw/$a" ]; then run "conv_${a%%_2*}" "$T/convert_test" "$HOME/fire_fw/$a"
        else printf '%-14s SKIP  (no %s here)\n' "convert" "$a"; fi
    done
else printf '%-14s FAIL  (does not build: %s)\n' convert_test "$(head -1 "$T/ct.err")"; fail=1; fi
run lights_vs_orc sh -c "'$T/lights_test' dump | python3 lights_vs_orc.py $HOME/orc/orc1/orc1_fire.py"
run wiring_test   python3 -B wiring_test.py
run menu_render   python3 -B menu_render.py "$T/menu_png"
run host_vu_test  python3 -B host_vu_test.py
if [ "$1" = "--mut" ]; then
    run wiring_mut  python3 -B wiring_mutate.py .
    run lights_mut  python3 -B lights_mutate.py "$HOME/orc/orc1/orc1_fire.py"
    run host_vu_mut python3 -B host_vu_mutate.py
fi
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOMETHING FAILED"
exit $fail
