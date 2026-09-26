#!/bin/sh
# Count every PMU event id from $2 to $3 (default 1..128) over workload mode $1 of pmuwork.
# Output: <id> <hex id> <count>
M=${1:-0}
i=${2:-1}
end=${3:-128}
N=${4:-20}
while [ $i -le $end ]; do
	c=$(perf stat -a -x, -e hexagon/event=$i/ -- pmuwork $M $N 2>&1 >/dev/null | grep -a hexagon | head -1 | cut -d, -f1)
	printf '%d 0x%x %s\n' $i $i "$c"
	i=$((i + 1))
done
