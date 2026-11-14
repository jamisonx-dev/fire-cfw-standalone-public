#!/bin/sh
# Akai Fire FLAME custom firmware: install / restore tool for macOS and Linux (the Windows one is fire_tool.ps1; this
# does the same steps in the same order). Started by "Install custom firmware.command" / "Restore original Akai
# firmware.command", or by hand:
#
#   sh files/fire_tool.sh install            back up the whole chip (read twice, compared), write the firmware
#   sh files/fire_tool.sh restore            put backups/ORIGINAL_Akai_firmware.bin back
#   sh files/fire_tool.sh restore <backup>   put any other full backup back
#
# install writes only the app area (0x08008000, 96 KB) and reads the chip back to check it. restore writes the app +
# pattern area (0x08008000-0x0803FFFF). Neither ever writes Akai's bootloader (0x08000000) or its settings
# (0x08004000), so the Fire can always be put back into update mode with the button combo.
#
# Needs dfu-util: macOS `brew install dfu-util`, Debian / Ubuntu / Raspberry Pi OS `sudo apt install dfu-util`.
# On Linux dfu-util runs through sudo (USB access), so it may ask for your password.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
BACKDIR="$ROOT/backups"
ORIGINAL="$BACKDIR/ORIGINAL_Akai_firmware.bin"
TMP=$(mktemp -d 2>/dev/null || mktemp -d -t firetool)
trap 'rm -rf "$TMP"' EXIT
CHIP=262144        # STM32F401RC: 256 KB
APP=98304          # sectors 2-4
REST=229376        # sectors 2-5 (app + pattern area)
TOKEN=EndOfFlashToken
SYSEX='F0 47 7F 43 72 00 00 F7'   # "go to update mode", understood by Akai's firmware and ours

if [ -t 1 ]; then R=$(printf '\033[31m'); G=$(printf '\033[32m'); Y=$(printf '\033[33m'); C=$(printf '\033[36m'); N=$(printf '\033[0m')
else R=; G=; Y=; C=; N=; fi
say() { printf '%s%s%s\n' "${2:-}" "$1" "$N"; }
fail() {
    say ''
    say "STOPPED: $1" "$R"
    say 'Nothing after this point was done. Your Fire is safe: unplug it and plug it back in.' "$Y"
    exit 1
}
choose() {   # choose "question" "k f c" -> sets ANS to the letter typed (only those letters count). Never call it in
    # $( ): fail would then only end the subshell and the job would go on.
    while :; do
        printf '%s: ' "$1"
        read -r a || fail 'no answer typed (no keyboard input). Nothing was written.'
        a=$(printf '%s' "$a" | tr 'A-Z' 'a-z' | tr -d ' ')
        for l in $2; do [ "$a" = "$l" ] && { ANS=$l; return; }; done
        case "$a" in [0-9]*) ;; ?*) f=$(printf '%s' "$a" | cut -c1); for l in $2; do [ "$f" = "$l" ] && { ANS=$l; return; }; done ;; esac
    done
}

# ---------------------------------------------------------------- dfu-util
command -v dfu-util >/dev/null 2>&1 || {
    say 'This needs the free tool dfu-util, which is not installed.' "$Y"
    case "$(uname)" in
        Darwin) say '  Install it with Homebrew (https://brew.sh):  brew install dfu-util' ;;
        *)      say '  Install it with your package manager, e.g.:  sudo apt install dfu-util' ;;
    esac
    fail 'dfu-util is missing.'
}
SUDO=
if [ "$(uname)" != Darwin ] && [ "$(id -u)" != 0 ]; then SUDO=sudo; fi
dfu() { $SUDO dfu-util "$@" 2>&1; }
dfu_present() { dfu -l | grep -qi '\[0483:df11\]'; }
wait_for_dfu() {   # wait_for_dfu <seconds>
    i=0; while [ "$i" -lt "$1" ]; do dfu_present && return 0; sleep 1; i=$((i + 1)); done; return 1
}

# ---------------------------------------------------------------- bytes
size_of() { wc -c <"$1" | tr -d ' '; }
slice() {          # slice <file> <offset> <length> > out  (offsets and lengths here are multiples of 16)
    dd if="$1" bs=16 skip=$(($2 / 16)) count=$(($3 / 16)) 2>/dev/null
}
same() {           # same <fileA> <offA> <fileB> <offB> <len>
    slice "$1" "$2" "$5" >"$TMP/sa"; slice "$3" "$4" "$5" >"$TMP/sb"; cmp -s "$TMP/sa" "$TMP/sb"
}
has_token_at() {   # has_token_at <file> <offset>
    [ "$(slice "$1" "$2" 16 | head -c 15)" = "$TOKEN" ]
}
is_blank() {       # is_blank <file> <offset> <length>: all 0xFF
    dd if=/dev/zero bs=16 count=$(($3 / 16)) 2>/dev/null | tr '\000' '\377' >"$TMP/ff"
    slice "$1" "$2" "$3" >"$TMP/sl"; cmp -s "$TMP/sl" "$TMP/ff"
}
is_akai_app() {    # our firmware carries an "ORCBUILD=" tag in its app area; Akai's does not
    ! slice "$1" 32768 98304 | LC_ALL=C grep -aq 'ORCBUILD='
}

# ---------------------------------------------------------------- MIDI: ask the running Fire to enter update mode
# Linux: amidi (alsa-utils). The Fire's port names: Akai "FL STUDIO FIRE", ours "ORC FW seq" (up to v0.35b),
# "FLAME seq" (v0.36+, the first of FLAME's two ports; "FLAME DAW" is the other and is never used here).
# macOS has no built-in MIDI command line tool, so there the button combo is the way in.
# First try: aplaymidi through the ALSA sequencer, which works while other programs use the Fire (amidi gets
# "Device or resource busy" then). It plays a tiny MIDI file holding just that SysEx. Second try: amidi (raw MIDI).
seq_kick() {
    command -v aplaymidi >/dev/null 2>&1 || return 1
    port=$(aplaymidi -l 2>/dev/null | awk 'NR > 1 && !/FLAME DAW/ && /FL STUDIO FIRE|ORC FW|FLAME seq/ { print $1; exit }')
    [ -n "$port" ] || return 1
    # MThd (format 0, 1 track, 96 ticks) + MTrk: delta 0, F0 <7> 47 7F 43 72 00 00 F7, end of track
    printf '\115\124\150\144\000\000\000\006\000\000\000\001\000\140\115\124\162\153\000\000\000\016\000\360\007\107\177\103\162\000\000\367\000\377\057\000' >"$TMP/dfu.mid"
    say "Found the Fire ($port). Asking it to switch to update mode (the screen goes dark)..."
    aplaymidi -p "$port" "$TMP/dfu.mid" 2>/dev/null
}
midi_kick() {
    seq_kick && return 0
    command -v amidi >/dev/null 2>&1 || { say 'No Fire found as a MIDI device.' "$Y"; return 1; }
    port=$(amidi -l 2>/dev/null | awk '{ d=$2; $1=""; $2=""; sub(/^ +/, ""); if ($0 ~ /FL STUDIO FIRE|ORC FW|^FLAME( seq)?$/) { print d; exit } }')
    [ -n "$port" ] || { say 'No Fire found as a MIDI device.' "$Y"; return 1; }
    say "Found the Fire ($port). Asking it to switch to update mode (the screen goes dark)..."
    amidi -p "$port" -S "$SYSEX" 2>/dev/null || say '  That did not work (is another program holding the port?).' "$Y"
    return 0
}

enter_update_mode() {
    if dfu_present; then say 'The Fire is already in update mode.'; return; fi
    if [ "$(uname)" != Darwin ] && midi_kick && wait_for_dfu 10; then :; else
        say ''
        say 'Please put the Fire into update mode by hand:' "$C"
        say '  1. Unplug the Fire'"'"'s USB cable.'
        say '  2. Hold down STEP + NOTE + DRUM + PERFORM (the four buttons bottom left).'
        say '  3. Keep holding them and plug the cable back in. Let go after 2 seconds.'
        say '     The screen and pads stay dark: that is update mode.'
        say 'Waiting up to 2 minutes...'
        wait_for_dfu 120 || fail 'the Fire never showed up in update mode.'
    fi
    say 'The Fire is in update mode.' "$G"
}

read_chip() {      # read_chip <file>
    rm -f "$1"
    out=$(dfu -a 0 -s 0x08000000:$CHIP -U "$1")
    if [ ! -f "$1" ] || [ "$(size_of "$1")" != "$CHIP" ]; then printf '%s\n' "$out"; fail 'could not read the Fire'"'"'s memory.'; fi
}
save_backup() {    # save_backup <suffix> -> sets BACKUP
    mkdir -p "$BACKDIR" || fail "could not make $BACKDIR"
    BACKUP="$BACKDIR/fire_backup_$(date +%Y-%m-%d_%H%M%S)$1.bin"
    read_chip "$BACKUP"
    read_chip "$TMP/check.bin"
    cmp -s "$BACKUP" "$TMP/check.bin" || fail 'the two reads of the Fire did not match.'
    has_token_at "$BACKUP" 131056 || fail 'the Fire'"'"'s memory does not look like a working Fire (no end marker). Nothing was written.'
    [ -z "$SUDO" ] || $SUDO chown "$(id -u):$(id -g)" "$BACKUP" 2>/dev/null
}
write_area() {     # write_area <file> <length>
    out=$(dfu -a 0 -s 0x08008000:"$2" -D "$1") || { printf '%s\n' "$out"; return 1; }
}
leave_update_mode() {
    dfu -a 0 -s 0x08000000:leave >/dev/null
    sleep 4
    dfu_present && say 'If the Fire stays dark, unplug it and plug it back in (no buttons held).' "$Y"
    return 0
}

# ---------------------------------------------------------------- the two jobs
MODE=${1:-}
say ''
case "$MODE" in
install)
    FW=$(ls "$HERE"/fire_custom_*.bin 2>/dev/null | head -1)
    [ -n "$FW" ] || fail 'the firmware file is missing from the "files" folder.'
    if [ "$(size_of "$FW")" != "$APP" ] || ! has_token_at "$FW" $((APP - 16)); then fail 'the firmware file is damaged (wrong size or end marker).'; fi
    say "Akai Fire custom firmware installer ($(basename "$FW"))" "$C"
    say 'Step 1 of 3: back up what is on your Fire now. Step 2: install. Step 3: check.'
    say 'Plug the Fire into this computer (close any music program using it first).'
    say ''
    enter_update_mode

    say ''
    say 'Step 1: reading your Fire'"'"'s whole memory (twice, to be sure)...'
    save_backup ''; A=$BACKUP
    say "Backup saved: $A" "$G"
    if is_akai_app "$A"; then
        if [ ! -f "$ORIGINAL" ]; then
            cp "$A" "$ORIGINAL"
            say "Your Fire's original Akai firmware is saved as: $ORIGINAL" "$G"
            say '  Keep this file. "Restore original Akai firmware" puts it back whenever you want.'
        else
            say "(The original Akai firmware was already saved earlier: $ORIGINAL)"
        fi
    elif [ ! -f "$ORIGINAL" ]; then
        say 'Note: this Fire was already running a custom firmware, so this backup is not Akai'"'"'s original.' "$Y"
    fi

    FORMAT=no
    if ! is_blank "$A" 131072 131072; then   # a Fire on Akai's firmware has this area empty: no question then
        say ''
        say 'This Fire already has saved patterns from an earlier install of this firmware.' "$Y"
        say '  K = keep them (the firmware loads them)'
        say '  F = format: erase them and start with no patterns'
        say '  C = cancel: stop here, nothing written'
        say 'Either way they are in the backup you just made, so Restore can bring them back.'
        choose 'Type K, F or C, then Enter' 'k f c'
        [ "$ANS" = c ] && fail 'cancelled. Nothing was written.'
        [ "$ANS" = f ] && FORMAT=yes
    fi

    say ''
    say 'Step 2: installing the custom firmware (about 10 seconds, do not unplug)...'
    IMG=$FW; LEN=$APP
    if [ "$FORMAT" = yes ]; then   # one write: the firmware followed by an empty (0xFF) pattern area
        { cat "$FW"; dd if=/dev/zero bs=16 count=$(((REST - APP) / 16)) 2>/dev/null | tr '\000' '\377'; } >"$TMP/format.bin"
        IMG="$TMP/format.bin"; LEN=$REST
        say '(and erasing the saved patterns)'
    fi
    write_area "$IMG" "$LEN" || fail 'writing failed. Unplug, hold STEP + NOTE + DRUM + PERFORM, plug in, and run Install again (or Restore).'

    say 'Step 3: checking...'
    read_chip "$TMP/check.bin"; Ck="$TMP/check.bin"
    same "$Ck" 0 "$A" 0 32768 || fail 'CHECK FAILED: the bootloader area changed (this should never happen). Run Restore.'
    same "$Ck" 32768 "$FW" 0 "$APP" || fail 'CHECK FAILED: the firmware did not write correctly. Run Install again or Restore.'
    if [ "$FORMAT" = yes ]; then is_blank "$Ck" 131072 131072 || fail 'CHECK FAILED: the pattern area did not erase. Run Install again or Restore.'
    else same "$Ck" 131072 "$A" 131072 131072 || fail 'CHECK FAILED: the saved patterns changed. Run Restore.'; fi
    say 'Checked: the custom firmware is on the Fire, Akai'"'"'s bootloader untouched.' "$G"
    leave_update_mode
    say ''
    say 'DONE. The Fire should now light up running the custom firmware.' "$G"
    ;;
restore)
    if [ -n "${2:-}" ]; then
        say 'Akai Fire: restore a backup' "$C"
        [ -f "$2" ] || fail "no such file: $2"
        PICK=$2
    elif [ -f "$ORIGINAL" ]; then
        say 'Akai Fire: put the original Akai firmware back' "$C"
        PICK=$ORIGINAL
    else
        say 'Akai Fire: put the original Akai firmware back' "$C"
        set -- $(ls "$BACKDIR"/fire_backup_*.bin 2>/dev/null | sort)
        [ $# -gt 0 ] || fail "no backup found in $BACKDIR. The original is saved by Install the first time it runs on a Fire with Akai's firmware."
        say "There is no ORIGINAL_Akai_firmware.bin in $BACKDIR (this Fire was not on Akai's firmware" "$Y"
        say 'when Install first ran here). These backups exist, oldest first:' "$Y"
        i=1; nums=c; for f in "$@"; do say "  $i. $(basename "$f")"; nums="$nums $i"; i=$((i + 1)); done
        choose 'Type a number to restore it, or C to cancel, then Enter' "$nums"
        [ "$ANS" = c ] && fail 'cancelled. Nothing was written.'
        eval "PICK=\${$ANS}"
    fi
    if [ "$(size_of "$PICK")" != "$CHIP" ] || ! has_token_at "$PICK" 131056; then fail "$(basename "$PICK") is not a complete Fire backup."; fi
    say "Restoring $(basename "$PICK")"
    say 'Plug the Fire into this computer (close any music program using it first).'
    say ''
    enter_update_mode

    say ''
    say 'First, backing up what is on the Fire now (your patterns included)...'
    save_backup '_before_restore'
    say "Saved: $BACKUP" "$G"
    if is_blank "$PICK" 131072 131072 && ! is_blank "$BACKUP" 131072 131072; then
        say 'Restoring clears the custom firmware'"'"'s saved patterns (they are in the backup just saved).'
    fi
    slice "$PICK" 32768 "$REST" >"$TMP/restore.bin"
    say ''
    say 'Writing the backup back (about 20 seconds, do not unplug)...'
    write_area "$TMP/restore.bin" "$REST" || fail 'writing failed. Unplug, hold STEP + NOTE + DRUM + PERFORM, plug in, and run Restore again.'
    say 'Checking...'
    read_chip "$TMP/check.bin"
    same "$TMP/check.bin" 32768 "$PICK" 32768 "$REST" || fail 'CHECK FAILED: the restore did not write correctly. Run Restore again.'
    same "$TMP/check.bin" 0 "$PICK" 0 32768 || say 'Note: Akai'"'"'s settings area differs from the backup (it was never written by this tool).' "$Y"
    say 'Checked: the Fire matches the backup.' "$G"
    leave_update_mode
    say ''
    if [ "$PICK" = "$ORIGINAL" ]; then say 'DONE. The Fire should now start up with Akai'"'"'s original firmware.' "$G"
    else say 'DONE. The Fire should now start up as it was when that backup was made.' "$G"; fi
    ;;
*)
    say 'usage: sh fire_tool.sh install | restore [backup.bin]'; exit 2 ;;
esac
