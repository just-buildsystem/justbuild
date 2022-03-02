#!/usr/bin/env python3

import os
import time

RESULTS = {}

time_start = time.time()
time_stop = 0

for attempt in os.listdir("."):
    if os.path.isdir(attempt):
        with open(os.path.join(attempt, "result")) as f:
            result = f.read().strip()
        RESULTS[result] = RESULTS.get(result, []) + [int(attempt)]
        try:
            with open(os.path.join(attempt, "time-start")) as f:
                time_start = min(time_start, float(f.read().strip()))
        except:
            pass
        try:
            with open(os.path.join(attempt, "time-stop")) as f:
                time_stop = max(time_start, float(f.read().strip()))
        except:
            pass

result = "UNKNOWN"
if set(RESULTS.keys()) <= set(["PASS", "FAIL"]):
    if not RESULTS.get("FAIL"):
        result = "PASS"
    elif not RESULTS.get("PASS"):
        result = "FAIL"
    else:
        result = "FLAKY"
with open("result", "w") as f:
    f.write("%s\n" % (result,))

with open("time-start", "w") as f:
    f.write("%d\n" % (time_start,))
with open("time-stop", "w") as f:
    f.write("%d\n" % (time_stop,))

with open("stdout", "w") as f:
    f.write("Summary: %s\n\n" % (result,))
    f.write("PASS: %s\n" % (sorted(RESULTS.get("PASS", [])),))
    f.write("FAIL: %s\n" % (sorted(RESULTS.get("FAIL", [])),))
    RESULTS.pop("PASS", None)
    RESULTS.pop("FAIL", None)
    if RESULTS:
        f.write("\nother results: %r\n" % (RESULTS,))

with open("stderr", "w") as f:
    pass
