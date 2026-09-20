#!/usr/bin/env python3
"""Record calibration points and fit the antenna delay.

  ./calibrate.py measure 5.0 "upright, faces toward"   # note is optional but wise
  ./calibrate.py fit             # fit antenna delay + show residuals
  ./calibrate.py set 16464       # write an antenna delay to both boards

Points accumulate in calibration.csv.
"""
import json, sys, time, csv, os, urllib.request, statistics as st

A, B = "http://uwb-a.local", "http://uwb-b.local"
CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "calibration.csv")
# Measured on this rig: changing antenna delay on BOTH boards moves the
# reported range by 9.55 mm per unit (~4.77 mm per board, matching 15.65 ps * c).
MM_PER_UNIT = 0.009546

def get(url, timeout=8):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.load(r)

def post(url):
    urllib.request.urlopen(urllib.request.Request(url, method="POST"), timeout=8).read()

def measure(true_d, note="", settle=3, secs=25):
    for h in (A, B):
        post(h + "/api/reset")
    print(f"collecting {secs}s at a true distance of {true_d} m ...")
    time.sleep(settle + secs)
    d = get(A + "/api/stats")
    if not d["ok"]:
        sys.exit("no successful exchanges - check both boards are up")
    row = dict(true_d=true_d, mean=d["mean"], sd=d["sd"], n=d["ok"],
               success=d["success"], antdly=d["antdly"], phy=d["phy"],
               rssi=d["rssi"], fp=d["fp"], ppm=d["ppm"], note=note)
    new = not os.path.exists(CSV)
    with open(CSV, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(row))
        if new: w.writeheader()
        w.writerow(row)
    sem = d["sd"] / (d["ok"] ** 0.5)
    print(f"  measured {d['mean']:.3f} m  sd={d['sd']:.3f}  n={d['ok']}  "
          f"sem={sem:.3f}  err={d['mean']-true_d:+.3f} m  fp={d['fp']} dBm")
    if d["success"] < 90:
        print(f"  WARNING: only {d['success']}% of exchanges succeeded - this "
              "point is from a marginal link and should not be trusted.")

def fit():
    if not os.path.exists(CSV):
        sys.exit("no calibration.csv yet - run 'measure <true distance>' first")
    rows = [r for r in csv.DictReader(open(CSV))]
    if not rows: sys.exit("calibration.csv is empty")
    # A different PHY profile has a different group delay, so its points cannot
    # be pooled with another profile's.
    phys = {r.get("phy") for r in rows if r.get("phy") not in (None, "")}
    if len(phys) > 1:
        sys.exit(f"points span more than one PHY profile {sorted(phys)} - "
                 "they cannot be fitted together. Archive calibration.csv and "
                 "re-measure on the profile you intend to deploy.")
    # Every point must share one antenna delay for a single-constant fit.
    dly = {int(r["antdly"]) for r in rows}
    if len(dly) > 1:
        print(f"warning: points recorded at different antenna delays {sorted(dly)};"
              " normalising all of them to the most recent.")
    # A point from a marginal link is not a calibration point. A weak first
    # path makes the leading edge estimator latch a later peak and read long,
    # so pooling these with good points corrupts the constant.
    MIN_SUCCESS = 90.0
    good = [r for r in rows if float(r["success"]) >= MIN_SUCCESS]
    dropped = len(rows) - len(good)
    if dropped:
        print(f"excluding {dropped} point(s) measured below {MIN_SUCCESS}% success:")
        for r in rows:
            if float(r["success"]) < MIN_SUCCESS:
                print(f"  {float(r['true_d']):5.2f} m at {r['success']}% success "
                      f"(fp {r['fp']} dBm) - link too weak to calibrate against")
    if not good:
        sys.exit("no points from a healthy link - re-measure with >90% success")
    rows = good
    cur = int(rows[-1]["antdly"])
    errs = []
    for r in rows:
        # Normalise each point to the current antenna delay setting.
        shift = (int(r["antdly"]) - cur) * MM_PER_UNIT
        errs.append(float(r["mean"]) + shift - float(r["true_d"]))
    bias = st.mean(errs)
    adj = round(bias / MM_PER_UNIT)
    print(f"\n{'true':>7} {'measured':>9} {'error':>8} {'fp dBm':>8}  note")
    for r, e in zip(rows, errs):
        print(f"{float(r['true_d']):7.2f} {float(r['mean']):9.3f} {e:+8.3f} "
              f"{r['fp'] if r['fp'] not in ('','None') else '-':>8}  {r.get('note','')}")
    notes = {r.get("note", "") for r in rows}
    if len(notes) > 1:
        print("\nNOTE: points carry different setup notes " + str(sorted(notes)) +
              ".\n  Antenna orientation changes the group delay - do not pool points\n"
              "  taken in different physical arrangements.")
    print(f"\nmean bias {bias:+.3f} m over {len(rows)} points")
    if len(errs) > 1:
        print(f"residual spread (sd of errors) {st.pstdev(errs):.3f} m")
        print("  -> a constant is sufficient if this is within your accuracy target;")
        print("     if errors trend with fp power, correct on power, not distance.")
    print(f"\nsuggested antenna delay: {cur} + {adj} = {cur + adj}")
    print(f"apply with:  ./calibrate.py set {cur + adj}")

def setdly(v):
    for h in (A, B):
        post(f"{h}/api/config?antdly={v}")
    print(f"antenna delay set to {v} on both boards (persisted in NVS)")

if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(__doc__)
    c = sys.argv[1]
    if c == "measure": measure(float(sys.argv[2]),
                               sys.argv[3] if len(sys.argv) > 3 else "")
    elif c == "fit": fit()
    elif c == "set": setdly(int(sys.argv[2]))
    else: sys.exit(__doc__)
