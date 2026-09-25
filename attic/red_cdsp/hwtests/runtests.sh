#!/bin/sh
# Run every test in the directories given (default /tests/hvx /tests/hmx), one line each:
#   PASS|FAIL <name> <rc>     (rc 132 = SIGILL, 136 = SIGFPE, 139 = SIGSEGV, 135 = SIGBUS)
[ $# -eq 0 ] && set -- /tests/hvx /tests/hmx
pass=0; fail=0
for d in "$@"; do
	for t in $d/*; do
		[ -x $t ] || continue
		n=${d##*/}/${t##*/}
		out=$($t 2>&1); rc=$?
		if [ $rc -eq 0 ]; then echo "PASS $n"; pass=$((pass+1)); else
			echo "FAIL $n rc=$rc: $(echo "$out" | tail -2 | tr '\n' ' ')"; fail=$((fail+1)); fi
	done
done
echo "SUMMARY pass=$pass fail=$fail"
