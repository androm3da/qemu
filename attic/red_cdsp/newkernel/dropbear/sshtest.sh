#!/bin/bash
# usage: sshtest.sh <outdir-name>   -- test host key types against the host ssh client under linux-user
S=/tmp/claude-1000/-home-brian-src-qemu/447866c9-a019-4c06-bd73-d713a270e957/scratchpad
Q=/home/brian/src/qemu/build_claude_hex/qemu-hexagon
D=$S/db/out/$1
for kt in ed25519 ecdsa; do
  K=$S/db/hk_$1_$kt; rm -f $K
  $Q $D/dropbearkey -t $kt ${2:+-s $2} -f $K >/dev/null 2>&1
  ok=$($Q $D/dropbearkey -y -f $K 2>&1 | head -1)
  P=$((3000 + RANDOM % 2000))
  ($Q $D/dropbear -F -E -s -w -r $K -p 127.0.0.1:$P > $S/db/log_$1_$kt.txt 2>&1 &)
  sleep 2
  case $kt in ed25519) HA=ssh-ed25519;; ecdsa) HA=ecdsa-sha2-nistp256;; esac
  r=$(timeout 60 ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o ConnectTimeout=20 -o HostKeyAlgorithms=$HA -p $P root@127.0.0.1 true 2>&1 | tail -1)
  pkill -f "dropbear -F -E -s -w -r $K" 2>/dev/null
  echo "$1 $kt: keyread='$ok' ssh='${r:-connected (auth then denied/ok)}'"
done
