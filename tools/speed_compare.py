"""speed_compare.py - compare speed_sweep runs across speeds, as distributions over seeds.

    py tools/speed_compare.py OUT/runs.jsonl > summary.txt

Missions cannot be paired across speeds (the speed changes START's seed), so each speed is a
sample of missions from the same generator. Reports per speed: flights, early exits, errors, the
pacing figures, enemy launches, bursts, misses and player hits, with bootstrap 95% intervals, and
the difference from the 9 MIPS baseline.
"""
import json
import random
import statistics
import sys
from collections import Counter

path = sys.argv[1]
rows = [json.loads(l) for l in open(path, encoding="utf-8")]
rng = random.Random(1)


def boot_ci(vals, stat=statistics.mean, n=4000):
    if not vals:
        return (float("nan"), float("nan"))
    ests = sorted(stat([rng.choice(vals) for _ in vals]) for _ in range(n))
    return ests[int(0.025 * n)], ests[int(0.975 * n)]


def diff_ci(a, b, n=4000):
    if not a or not b:
        return (float("nan"), float("nan"))
    ds = sorted(statistics.mean([rng.choice(a) for _ in a]) - statistics.mean([rng.choice(b) for _ in b])
                for _ in range(n))
    return ds[int(0.025 * n)], ds[int(0.975 * n)]


by_speed = {}
for r in rows:
    by_speed.setdefault(str(r["speed"]), []).append(r)

print(f"{'speed':>6} {'n':>4} {'err':>4} {'early':>5} {'complete':>8} {'fps':>7} {'S':>6} {'swings':>6} {'clock':>6}")
complete = {}
for sp in sorted(by_speed, key=lambda s: int(s)):
    rs = by_speed[sp]
    err = [r for r in rs if "error" in r or "fps" not in r]
    ok = [r for r in rs if "fps" in r and not r.get("exited") and "error" not in r]
    early = [r for r in rs if r.get("exited")]
    complete[sp] = ok
    if ok:
        fps = statistics.mean(r["fps"] for r in ok)
        s = statistics.mean(r["S_mean"] for r in ok)
        sw = sum(r["swings"] for r in ok)
        clk = statistics.mean(r["clock_per_s"] for r in ok)
    else:
        fps = s = clk = float("nan"); sw = 0
    print(f"{sp:>6} {len(rs):>4} {len(err):>4} {len(early):>5} {len(ok):>8} {fps:>7.2f} {s:>6.2f} {sw:>6} {clk:>6.3f}")

print()
print("Enemy and player combat per flight (complete flights; intervals are 95% bootstrap):")
print(f"{'speed':>6} {'launch/fl':>10} {'burst/fl':>9} {'miss/fl':>8} {'hits/fl':>8} {'fl w/ hit':>10} {'fl w/ launch':>13}")
stats = {}
for sp in sorted(complete, key=lambda s: int(s)):
    ok = complete[sp]
    L = [r["enemy_launches"] for r in ok]
    B = [r["enemy_bursts"] for r in ok]
    M = [r["enemy_misses"] for r in ok]
    H = [max(0, r["damage_hits"] - 2) for r in ok]
    stats[sp] = dict(L=L, B=B, M=M, H=H)
    hitfl = sum(1 for h in H if h)
    lfl = sum(1 for l in L if l)
    lo, hi = boot_ci(L)
    print(f"{sp:>6} {statistics.mean(L):>6.2f} ({lo:.2f}-{hi:.2f}) {statistics.mean(B):>9.2f} "
          f"{statistics.mean(M):>8.2f} {statistics.mean(H):>8.3f} {hitfl:>4}/{len(ok):<4} {lfl:>6}/{len(ok):<6}")

print()
base = "9"
if base in stats:
    for sp in sorted(stats, key=lambda s: int(s)):
        if sp == base:
            continue
        for key, label in (("L", "launches per flight"), ("H", "player hits per flight"), ("B", "bursts per flight")):
            a, b = stats[sp][key], stats[base][key]
            d = statistics.mean(a) - statistics.mean(b)
            lo, hi = diff_ci(a, b)
            print(f"{sp} vs {base} MIPS, {label}: difference {d:+.3f} (95% {lo:+.3f} to {hi:+.3f})")
        # Whether a flight is hit at all: 2x2 counts.
        hit_a = sum(1 for h in stats[sp]["H"] if h); hit_b = sum(1 for h in stats[base]["H"] if h)
        n_a, n_b = len(stats[sp]["H"]), len(stats[base]["H"])
        print(f"{sp} vs {base} MIPS, flights with a player hit: {hit_a}/{n_a} vs {hit_b}/{n_b}")
        print()

print("Mission mix (primary objective, from each complete flight's end state):")
for sp in sorted(complete, key=lambda s: int(s)):
    c = Counter(r.get("objective") for r in complete[sp])
    print(f"  {sp:>3} MIPS: " + ", ".join(f"obj{k}:{v}" for k, v in sorted(c.items(), key=lambda kv: str(kv[0]))))
