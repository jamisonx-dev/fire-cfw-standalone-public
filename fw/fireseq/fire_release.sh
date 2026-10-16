#!/bin/sh
# fire_release.sh <label> -- build, test and flash the Fire firmware from THIS tree, with every check the hand-run
# loop had (each one exists because skipping it bit someone). Run on the Pi, in
# ~/fire_fw/fw/fireseq, after copying the sources in. The Fire goes dark for about a minute: tell the owner first.
#
#   sh fire_release.sh v021              flash fireseq_v021_<date> (keeps .bin + .elf in ~/fire_fw)
#   sh fire_release.sh v021 --dry-run    everything except touching the Fire
#
# Steps: make (refuses a stale bin) -> run_tests.sh -> keep bin + ELF -> DFU -> full-chip backup -> the backup's app
# region must equal the image the Fire was running (~/fire_fw/FLASHED) -> app -> boot -> wait for the port ->
# crash check -> record ~/fire_fw/FLASHED. Never writes sectors 0, 1 or 5 (fire_flash.sh's rule).
set -u
LABEL=${1:?usage: fire_release.sh <label> [--dry-run]}
DRY=${2:-}
TREE=$(cd "$(dirname "$0")" && pwd)
FW=$HOME/fire_fw
DATE=$(date +%Y%m%d)
NAME=fireseq_${LABEL}_${DATE}
say() { echo "[release] $*"; }
die() { echo "[release] STOP: $*"; exit 1; }

cd "$TREE" || die "no tree"
[ -e "$FW/$NAME.bin" ] && die "$FW/$NAME.bin exists already (pick a new label)"
say "building"
make >"$FW/.release_make.log" 2>&1 || { tail -20 "$FW/.release_make.log"; die "make failed"; }
make -q fireseq.bin || die "make still thinks fireseq.bin is out of date (a stale image)"
BUILD=$(strings fireseq.bin | grep -o "ORCBUILD=[0-9a-f]*" | head -1)
[ -n "$BUILD" ] || die "no ORCBUILD tag in the image"
say "image $BUILD ($(stat -c%s fireseq.bin) bytes)"
say "testing"
sh run_tests.sh || die "a test failed: not flashing"
[ "$DRY" = "--dry-run" ] && { say "dry run: built + tested, nothing kept, the Fire untouched"; exit 0; }
cp fireseq.bin "$FW/$NAME.bin" && cp fireseq.elf "$FW/$NAME.elf" || die "cannot keep the image"
say "kept $FW/$NAME.bin + .elf"

cd "$FW" || die "no $FW"
PREV=$(cat FLASHED 2>/dev/null || true)
say "to update mode"
sh fw/fire_flash.sh dfu >/dev/null 2>&1
i=0; while ! lsusb | grep -q 0483:df11; do i=$((i + 1)); [ $i -gt 25 ] && die "the Fire did not enter update mode"; sleep 1; done
say "full-chip backup"
sudo dfu-util -a 0 -s 0x08000000:262144 -U "full_chip_before_${LABEL}_${DATE}.bin" >/dev/null 2>&1
[ "$(stat -c%s "full_chip_before_${LABEL}_${DATE}.bin" 2>/dev/null)" = 262144 ] || die "backup failed (the Fire is in update mode: fire_flash.sh boot to leave)"
if [ -n "$PREV" ] && [ -f "$PREV" ]; then
    dd if="full_chip_before_${LABEL}_${DATE}.bin" bs=1 skip=32768 count=98304 2>/dev/null | cmp -s - "$PREV" \
        && say "backup's app = $PREV (the backup is real)" \
        || say "WARNING: the backup's app is not $PREV (the Fire was running something else; the backup still holds it)"
fi
say "flashing"
sh fw/fire_flash.sh app "$NAME.bin" 2>&1 | tail -1 | tee .release_flash.log
grep -q "verify: OK" .release_flash.log || die "the flash did not verify (still in update mode: flash a known-good bin, or fire_flash.sh boot)"
sh fw/fire_flash.sh boot >/dev/null 2>&1
i=0; until python3 -c "import mido,sys; sys.exit(0 if any('ORC FW' in n for n in mido.get_output_names()) else 1)" 2>/dev/null; do
    i=$((i + 1)); [ $i -gt 30 ] && die "the Fire's MIDI port did not come back (lsusb: $(lsusb | grep -E '0483:df11|1209:0001'))"; sleep 1
done
echo "$FW/$NAME.bin" > FLASHED
sleep 2
say "running: $(cd "$TREE" && timeout 15 python3 -B host_crash.py | head -1)"
say "done: $NAME ($BUILD). The ORC repaints its surface by itself within ~30 s."
