#!/bin/bash
# The two guards every renderer speed change must pass (docs/gpu-compositor.md, last section).
#   guards.sh checksums TAG [NAME=value ...]   composed-image checksums at fixed cadence, Near/Far/Maximum,
#                                              compared with the baseline in private/renderer-bench/baseline
#   guards.sh baseline                         (re)write that baseline from the current build
#   guards.sh statehash                        the replay's final machine hash, ONTests build against the current one
# A speed change is accepted only when every distance prints IDENTICAL and the two final hashes agree
# (the hash of the replay is 600306c77cb04481 as of 5 November 2026).
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${F117_REPO:-$(cd "$HERE/../.." && pwd)}
WORK=${BENCH_WORK:-$HOME/f117-bench}
BASE=${BASE:-$ROOT/private/renderer-bench/baseline}
C="F117_REPLAY_FIXED_CADENCE=1 F117_PRESENT_HASH_FROM=3900000000 F117_REMASTER_THREADS=8"
hashes() {   # TAG DEST [env...]
  local tag=$1 dest=$2; shift 2
  mkdir -p "$dest"
  for d in near far maximum; do
    "$HERE/bench.sh" "h_${tag}_$d" $d $C "$@" >/dev/null 2>&1
    grep -h -i "hash" "$WORK/h_${tag}_$d/out.txt" | grep -v native > "$dest/hash_${tag}_$d.txt"
    echo "$tag $d: $(wc -l < "$dest/hash_${tag}_$d.txt") checksums"
  done
}
case "$1" in
  baseline) hashes base "$BASE" ;;
  checksums)
    tag=$2; shift 2
    hashes "$tag" "$WORK" "$@" >/dev/null
    for d in near far maximum; do
      if cmp -s "$BASE/hash_base_$d.txt" "$WORK/hash_${tag}_$d.txt"; then echo "$d: IDENTICAL ($(wc -l < "$WORK/hash_${tag}_$d.txt") checksums)"
      else echo "$d: DIFFERENT"; diff "$BASE/hash_base_$d.txt" "$WORK/hash_${tag}_$d.txt" | head -3; fi
    done ;;
  statehash)
    OLD=${OLD:-$HOME/Desktop/ONTests/f117a.exe}
    # Archived executables may share the current build's unchanged runtime DLLs.
    runtime_dir=$(dirname "${EXE:-$ROOT/build/f117a.exe}")
    if command -v cygpath >/dev/null 2>&1; then runtime_dir=$(cygpath -u "$runtime_dir"); fi
    export PATH="$runtime_dir:$PATH"
    for d in near maximum; do
      for b in old new; do
        E=${EXE:-$ROOT/build/f117a.exe}; [ $b = old ] && E=$OLD
        EXE=$E MODE=gpu XARGS="--log $WORK/sh_${b}_$d.log" "$HERE/bench.sh" "sh_${b}_$d" $d F117_REPLAY_FIXED_CADENCE=1 >/dev/null 2>&1
        final=$(grep -h 'final hash' "$WORK/sh_${b}_$d.log" | tail -1)
        if [ -z "$final" ]; then
          echo "$b $d: FAILED (no final hash; inspect $WORK/sh_${b}_$d/out.txt)" >&2
          exit 1
        fi
        echo "$b $d: $final"
      done
    done ;;
  *) echo "usage: guards.sh baseline | checksums TAG [NAME=value ...] | statehash"; exit 2 ;;
esac
