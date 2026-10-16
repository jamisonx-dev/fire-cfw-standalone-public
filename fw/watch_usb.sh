#!/bin/sh
# Log Fire USB identity changes for N seconds (default 15), 0.25 s resolution.
N=${1:-15}; last=""; t0=$(date +%s.%N)
end=$(echo "$t0 + $N" | bc)
while [ "$(echo "$(date +%s.%N) < $end" | bc)" = 1 ]; do
  cur=$(lsusb | grep -oE '09e8:0043|0483:df11' | tr '\n' ' ')
  [ -z "$cur" ] && cur="(none)"
  if [ "$cur" != "$last" ]; then printf '%6.2fs  %s\n' "$(echo "$(date +%s.%N) - $t0" | bc)" "$cur"; last="$cur"; fi
  sleep 0.25
done
