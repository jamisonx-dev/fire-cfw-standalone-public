#!/bin/sh
# slip_soak.sh [seconds] -- hands-off soak of the v0.29d TIM4 stack check (2026-10-26). Repeats fire_live_test.py
# (lights, host text / VU, idle, lazy save, external clock) then 3 min untouched, and after each step reads the crash
# record. Stops at the FIRST record (a slip or any crash), so it can be read before anything overwrites it; orc1-firevu
# also saves it to ~/fire_fw/crashes by itself. Log: ~/fire_fw/slip_soak_<date>.log. Run from the tree you flashed.
cd "$(dirname "$0")" || exit 1
LOG=$HOME/fire_fw/slip_soak_$(date +%Y%m%d).log
end=$(( $(date +%s) + ${1:-7200} ))
n=0
rec() { timeout 20 python3 -B host_crash.py --elf auto 2>&1 | head -1; }
echo "soak start $(date +%T) $(strings fireseq.bin | grep -o 'ORCBUILD=[0-9a-f]*')" >> "$LOG"
while [ "$(date +%s)" -lt "$end" ]; do
  n=$((n + 1))
  echo "--- cycle $n $(date +%T): live test" >> "$LOG"
  timeout 900 python3 -B fire_live_test.py >> "$LOG" 2>&1; echo "live test exit $?" >> "$LOG"
  r=$(rec); echo "$(date +%T) record: $r" >> "$LOG"
  case "$r" in "no crash on record") ;; *) echo "STOPPED: a record (cycle $n)" >> "$LOG"; break ;; esac
  sleep 180
  r=$(rec); echo "$(date +%T) after idle: $r" >> "$LOG"
  case "$r" in "no crash on record") ;; *) echo "STOPPED: a record (cycle $n, idle)" >> "$LOG"; break ;; esac
done
echo "soak end $(date +%T) after $n cycles" >> "$LOG"
