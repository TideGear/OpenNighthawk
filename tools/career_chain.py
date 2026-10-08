#!/usr/bin/env python3
"""career_chain.py: an earned career, sortie after sortie, on this machine.

    py tools/career_chain.py --data GOG_DIR --front FRONT --out DIR [--sorties 99] [--start-roster FILE]

Each sortie boots the game with the previous sortie's saved ROSTER.FIL in a fresh save directory and
flies recon_pilot.py's closed-loop career sortie from FRONT (--complete --extend --debrief: both
photos, the flight home, the landing, END's screens; START then saves the roster). After each sortie
the selected pilot's record is read (career_check.career) and checked (career_check.career_errors):
the sortie count advances by one, the score is added to the total, the rank never falls. The chain
stops at the first sortie that fails that check, so every saved roster in it is one a player could
have earned. DIR/NN holds each sortie's run and DIR/chain.json the records.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import career_check  # noqa: E402

# Regular opponents and Realistic Landings (tools/routes/career_regular_realistic.front) score about 296,
# above rank 6's average of 280; Realistic Landings stops nothing on the deck, so it is approached at
# 190-210 (a 250 touchdown ran off its end), and --cycle finds the secondary target.
PILOT = ["--complete", "--extend", "--cycle", "--debrief", "--landing-throttle-gain", "0.6", "--deck-pitch-floor", "-300",
         "--deck-speed", "190", "210", "30", "--seconds", "2400"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--front", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--sorties", type=int, default=99)
    ap.add_argument("--start-roster", type=Path, help="continue from this saved roster (default: the install's)")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    log = a.out / "chain.json"
    chain = json.loads(log.read_text()) if log.exists() else []
    roster = a.start_roster or (Path(a.data) / "ROSTER.FIL")
    if chain:
        roster = a.out / ("%02d" % len(chain)) / "ROSTER.FIL"
    while True:
        before = career_check.career(roster.read_bytes())
        if before["sorties"] >= a.sorties:
            break
        n = len(chain) + 1
        run = a.out / ("%02d" % n)
        if run.exists():
            shutil.rmtree(run)
        rc = subprocess.run([sys.executable, str(HERE / "recon_pilot.py"), "--data", a.data, "--out", str(run),
                             "--front-route", a.front, "--initial-roster", str(roster)] + PILOT,
                            stdout=open(a.out / ("%02d.log" % n), "w"), stderr=subprocess.STDOUT).returncode
        saved = run / "ROSTER.FIL"
        if not saved.exists():
            print("sortie", n, "saved no roster (rc %d)" % rc, flush=True)
            break
        after = career_check.career(saved.read_bytes())
        before_same = dict(before, pilot=after["pilot"]) if before["pilot"] != after["pilot"] else before
        errors = career_check.career_errors(before_same, after)
        result = json.loads((run / "result.json").read_text())
        chain.append(dict(sortie=n, rc=rc, flight_errors=result.get("errors", []), before=before, after=after,
                          career_errors=errors))
        log.write_text(json.dumps(chain, indent=1) + "\n")
        print("sortie %d: rank %d score %d total %d sorties %d status %d%s" % (
            n, after["rank"], after["score"], after["total"], after["sorties"], after["status"],
            "" if not errors else "  " + "; ".join(errors)), flush=True)
        if errors or after["status"]:
            break
        roster = saved
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
