#!/bin/bash
# Runway-replay timing for the Remastered renderer.
#   bench.sh NAME DISTANCE [NAME=value ...]      DISTANCE: near | far | maximum | original
# Prints: frames, mean, p50, p95, p99 of the work per frame (cost_ns), clocks 3.9 to 4.8 billion.
# Environment (all optional):
#   MODE=software|gpu   software (default): dummy video, software renderer. gpu: a hidden Direct3D 11 window.
#   WIN=WxH             window size (default 1920x1080; 3840x2160 with PSCALE=9 for 4K)
#   PSCALE=N            --present-scale (default 3)
#   EXE=PATH            the f117a.exe to run (default build/f117a.exe)
#   XARGS="..."         extra f117a options (for example --pick-order fast, --log FILE)
#   EXITAT=CLOCKS       --exit-after (default 4800000000)
#   BENCH_WORK=DIR      where runs are written (default ~/f117-bench)
#   SESSION, INI, GAME  the recorded flight, the ini its fix list needs, the game folder
# Extra NAME=value arguments become environment variables for the run (F117_GPU_COMPOSE=1 ...).
# Docs: docs/gpu-compositor.md, last section. Data lives in private/renderer-bench (git-ignored).
name=$1; dist=$2; shift 2
ROOT=${F117_REPO:-$(cd "$(dirname "$0")/../.." && pwd)}
WORK=${BENCH_WORK:-$HOME/f117-bench}
SESSION=${SESSION:-$ROOT/private/renderer-bench/session-20261103-154444}
INI=${INI:-$ROOT/private/renderer-bench/f117a.ini}
GAME=${GAME:-D:/GOG/F-117A}
D=$WORK/$name; rm -rf "$D"; mkdir -p "$D"; cp -r "$SESSION/save-start" "$D/save"
for kv in "$@"; do export "$kv"; done
export F117_WINDOW=${WIN:-1920x1080} SDL_AUDIODRIVER=dummy
if [ "${MODE:-software}" = gpu ]; then
  export F117_HIDDEN_WINDOW=1 SDL_RENDER_DRIVER=${RDRV:-direct3d11}
else
  export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
fi
"${EXE:-$ROOT/build/f117a.exe}" --data "$GAME" --save "$D/save" --replay "$SESSION/input.log" \
  --config "$INI" --presentation ${PRES:-remastered} --present-scale ${PSCALE:-3} --draw-distance "$dist" $XARGS \
  --exit-after ${EXITAT:-4800000000} --present-log "$D/log.txt" > "$D/out.txt" 2>&1
py -I - "$D/log.txt" <<'PY'
import sys
c = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 7 and p[0].isdigit() and int(p[1]) > 3900000000:
        c.append(int(p[6]) / 1e6)
c.sort(); n = len(c)
print(n, "frames mean %.2f p50 %.2f p95 %.2f p99 %.2f" % (sum(c) / n, c[n // 2], c[int(n * .95)], c[int(n * .99)]))
PY
