#!/usr/bin/env python3
"""Log a walk test to CSV so it can be reviewed afterwards.

  ./logwalk.py                    # log until Ctrl-C
  ./logwalk.py walk-driveway.csv  # to a named file

Polls the initiator twice a second and records distance, spread, success rate
and the NLOS discriminator. Counters are reset at the start so the rolling
window reflects the walk and not whatever came before it.
"""
import json, sys, time, csv, urllib.request

A = "http://uwb-a.local"
out = sys.argv[1] if len(sys.argv) > 1 else "walk.csv"

def post(u):
    urllib.request.urlopen(urllib.request.Request(u, method="POST"), timeout=8).read()
def get(u):
    with urllib.request.urlopen(u, timeout=5) as r: return json.load(r)

post(A + "/api/reset")
t0 = time.time()
cols = ["t", "dist", "sd", "success", "rate", "rssi", "fp", "fp_live", "nlos_gap", "ok", "timeout", "rx_error"]
print(f"logging to {out} - Ctrl-C to stop\n")
print(f"{'t':>6} {'dist':>8} {'succ':>6} {'fp live':>9} {'gap':>7}")
n = 0
try:
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols); w.writeheader()
        while True:
            try: d = get(A + "/api/stats")
            except Exception:
                # A dropout is data, not an error - record the gap and continue.
                time.sleep(0.5); continue
            gap = (d["rssi"] - d["fp"]) if (d["rssi"] is not None and d["fp"] is not None) else None
            row = dict(t=round(time.time() - t0, 1), dist=d["last_dist"], sd=d["sd"],
                       success=d["success"], rate=d["rate"], rssi=d["rssi"], fp=d["fp"],
                       fp_live=d["fp_fast"], nlos_gap=None if gap is None else round(gap, 1),
                       ok=d["ok"], timeout=d["timeout"], rx_error=d["rx_error"])
            w.writerow(row); f.flush()
            n += 1
            if n % 4 == 0:   # print once a second
                fmt = lambda v, p="": "-" if v is None else f"{v}{p}"
                print(f"{row['t']:6.1f} {fmt(row['dist']):>8} {fmt(row['success'],'%'):>6} "
                      f"{fmt(row['fp_live']):>9} {fmt(row['nlos_gap']):>7}")
            time.sleep(0.5)
except KeyboardInterrupt:
    print(f"\nstopped - {n} samples written to {out}")
