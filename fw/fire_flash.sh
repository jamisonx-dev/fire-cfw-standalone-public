#!/bin/sh
# Fire flashing loop, run on the Pi (~/fire_fw/fw/fire_flash.sh). No buttons needed.
#
#   fire_flash.sh dfu              stock or our firmware -> ST ROM DFU (SysEx 0x72)
#   fire_flash.sh app <file.bin>   write a 96 KB app image to 0x08008000 (sectors 2-4), verify
#   fire_flash.sh boot [secs]      leave DFU through Akai's bootloader, log USB changes
#   fire_flash.sh stock            put Akai's app back, verify the WHOLE chip = backup
#
# Akai's bootloader (sector 0) and the settings (sector 1) are never written here, and neither is
# sector 5 (our firmware's saved patterns).
set -e
cd "$HOME/fire_fw"
BACKUP=fire_flash_v0.21_a.bin
PORT='FL STUDIO FIRE:FL STUDIO FIRE Jack 1 32:0'

in_dfu() { lsusb | grep -q 0483:df11; }

case "$1" in
dfu)
    if in_dfu; then echo "already in DFU"; exit 0; fi
    python3 - "$PORT" <<'PY'
import sys, time, mido
name = next((n for n in mido.get_output_names() if 'FL STUDIO FIRE' in n or 'ORC FW' in n), None)
if not name:
    sys.exit("no Fire MIDI port (our firmware may not speak MIDI yet: use the button combo or wait for its own DFU reboot)")
o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x47, 0x7F, 0x43, 0x72, 0x00, 0x00]))  # cmd 0x72 = reboot to DFU (NOT 0x78)
time.sleep(0.3); o.close()
PY
    for i in 1 2 3 4 5 6 7 8 9 10; do sleep 0.5; in_dfu && { echo "in DFU"; exit 0; }; done
    echo "Fire did not enter DFU"; exit 1 ;;
app)
    [ -f "$2" ] || { echo "usage: $0 app <file.bin>"; exit 1; }
    [ "$(stat -c%s "$2")" = 98304 ] || { echo "image must be exactly 98304 bytes"; exit 1; }
    tail -c 16 "$2" | grep -q EndOfFlashToken || { echo "image has no EndOfFlashToken at the end"; exit 1; }
    in_dfu || { echo "not in DFU (run: $0 dfu)"; exit 1; }
    sudo dfu-util -a 0 -s 0x08008000:98304 -D "$2" 2>&1 | grep -vE '\[' | tail -2
    sudo dfu-util -a 0 -s 0x08000000:262144 -U /tmp/fire_check.bin >/dev/null 2>&1
    python3 - "$2" <<'PY'
import sys
a = open('fire_flash_v0.21_a.bin', 'rb').read(); b = open('/tmp/fire_check.bin', 'rb').read(); n = open(sys.argv[1], 'rb').read()
ok = a[:0x8000] == b[:0x8000] and b[0x8000:0x20000] == n
used = sum(1 for k in range(0x20000, 0x40000, 1024) if b[k:k+1024] != bytes([0xFF]) * 1024)
print('verify:', 'OK (app written, bootloader/settings untouched)' if ok else 'MISMATCH', f'| pattern store (sector 5): {used} of 128 records used, not touched')
sys.exit(0 if ok else 1)
PY
    sudo rm -f /tmp/fire_check.bin ;;
boot)
    in_dfu || { echo "not in DFU"; exit 1; }
    sh fw/watch_usb.sh "${2:-15}" > /tmp/fire_usbwatch.txt &
    sleep 0.5
    sudo dfu-util -a 0 -s 0x08000000:leave >/dev/null 2>&1 || true
    wait; cat /tmp/fire_usbwatch.txt; rm -f /tmp/fire_usbwatch.txt ;;
stock)
    in_dfu || "$0" dfu
    dd if=$BACKUP of=/tmp/fire_stock_app.bin bs=16384 skip=2 count=6 2>/dev/null
    sudo dfu-util -a 0 -s 0x08008000:98304 -D /tmp/fire_stock_app.bin 2>&1 | grep -vE '\[' | tail -1
    sudo dfu-util -a 0 -s 0x08000000:262144 -U /tmp/fire_check.bin >/dev/null 2>&1
    if [ "$(head -c 131072 /tmp/fire_check.bin | md5sum)" = "$(head -c 131072 $BACKUP | md5sum)" ]; then echo "verify: bootloader + settings + Akai app == backup"; else echo "verify: MISMATCH"; exit 1; fi
    if [ "$(tail -c 131072 /tmp/fire_check.bin | md5sum)" != "$(tail -c 131072 $BACKUP | md5sum)" ]; then echo "note: sector 5 holds our saved patterns (left in place; Akai's firmware ignores it)"; fi
    sudo rm -f /tmp/fire_check.bin /tmp/fire_stock_app.bin
    sudo dfu-util -a 0 -s 0x08000000:leave >/dev/null 2>&1 || true
    sleep 4; lsusb | grep -q 09e8:0043 && echo "stock Fire is back" ;;
*)
    sed -n '2,10p' "$0"; exit 1 ;;
esac
