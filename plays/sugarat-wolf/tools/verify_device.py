"""Reset the badge, then drive every interaction over the serial console and print the wolf log lines.
Usage: verify_device.py <port>"""
import sys
import time

import serial

port = sys.argv[1]
script = [(12, None), (0, b"~"), (8, None), (0, b"j"), (4, None), (0, b"j"), (0.15, None), (0, b"j"), (5, None),
          (0, b"k"), (7, None), (0, b"m"), (5, None), (0, b"t"), (5, None), (0, b"n"), (11, None)]
with serial.Serial(port, 115200, timeout=0.1) as s:
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    start = time.time()
    for wait, key in script:
        if key:
            s.write(key)
            print(f"{time.time() - start:6.2f} >> {key.decode()}")
        end = time.time() + wait
        while time.time() < end:
            line = s.readline().decode("utf8", "replace").rstrip()
            if any(k in line for k in ("wolf", "Wolf", "E (", "W (", "Guru", "abort", "panic", "jank", "frame")):
                print(f"{time.time() - start:6.2f} {line}")
