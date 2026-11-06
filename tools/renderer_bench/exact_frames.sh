#!/bin/bash
# Broad GPU-vs-CPU test for step 2: ten fixed-cadence frames at each of Near, Far and Maximum, recorded with one build in one mode (six recordings at a time),
# then gpu_chain on each frame with the same mode. Usage: exact_frames.sh WORKTREE MODE(float|exact) TAG
W=${1:-/d/f117-wt/exact-raster}; MODE=${2:-exact}; TAG=${3:-run}
T=/d/f117-dl/frames/$TAG; mkdir -p "$T"
export F117_REPO="/c/Dev/F-117A Recomp" W MODE TAG T
if [ "$MODE" = exact ]; then export X="F117_RASTER_EXACT=1"; else export X="F117_RASTER_EXACT=0"; fi
OUT=$T/summary.txt; : > "$OUT"
cd "$W" || exit 1

record() {   # distance k
  d=$1; k=$2
  from=$((3950000000 + k * 80000000))
  tr=$T/${d}_$k.trc
  [ -s "$tr" ] && return 0
  EXE=$W/build/f117a.exe EXITAT=$((from + 60000000)) bash tools/renderer_bench/bench.sh fr_${TAG}_${d}_$k $d F117_REMASTER_THREADS=1 F117_REPLAY_FIXED_CADENCE=1 "F117_GPU_DUMP=$(cygpath -m "$tr"),$from" $X > /dev/null 2>&1
}
export -f record
for d in near far maximum; do for k in 0 1 2 3 4 5 6 7 8 9; do echo "$d $k"; done; done | xargs -P 6 -L 1 bash -c 'record $0 $1'

for d in near far maximum; do
  for k in 0 1 2 3 4 5 6 7 8 9; do
    from=$((3950000000 + k * 80000000))
    tr=$T/${d}_$k.trc
    if [ ! -s "$tr" ]; then echo "$d $k clock $from: NO TRACE" >> "$OUT"; continue; fi
    r=$(cd "$W/build" && env $X ./gpu_chain.exe "$(cygpath -m "$tr")" . 2>&1 | grep "^chain" | sed 's/chain, [0-9]* x [0-9]* canvas: //')
    echo "$d $k clock $from: ${r:-NO RESULT}" >> "$OUT"
  done
done
echo done >> "$OUT"
