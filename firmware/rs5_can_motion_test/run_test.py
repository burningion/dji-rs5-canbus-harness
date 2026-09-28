#!/usr/bin/env python3
"""Supervise the fixed bench sequence; default is a read-only query check."""
import argparse
from pathlib import Path
import re
import time

import serial


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('--port', required=True)
    args.add_argument('--run', action='store_true', help='Run three fixed motion cycles after telemetry verification')
    args.add_argument('--log', type=Path, required=True)
    opts = args.parse_args()
    opts.log.parent.mkdir(parents=True, exist_ok=True)
    completed = False
    run_sent = False
    saw_start = False
    next_ping = 0.0
    next_status_print = 0.0
    started = time.monotonic()
    deadline = started + 8
    finish_at = None
    with serial.Serial(opts.port, 115200, timeout=0.05, write_timeout=0.2) as port, opts.log.open('w') as log:
        def record(line, force=False):
            nonlocal next_status_print
            log.write(line + '\n')
            log.flush()
            now = time.monotonic()
            if force or line.startswith(('HOST:', 'RUN ', 'PHASE ', 'DONE', 'ABORT', 'STOPPED', 'PROBE:')) or (
                    line.startswith('BENCH ') and now >= next_status_print):
                print(line, flush=True)
                next_status_print = now + 1

        try:
            port.write(b'help\nprobe\n')
            record('HOST: read-only joint verification; no motion until valid replies.')
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_ping:
                    port.write(b'ping\n')
                    next_ping = now + .2
                line = port.readline().decode('utf-8', errors='replace').strip()
                if line:
                    record(line)
                    if line.startswith(('ABORT', 'RUN REFUSED')):
                        break
                    if line.startswith('RUN START'):
                        saw_start = True
                    if line.startswith('DONE reason=completed zero_and_release_sent=yes') and saw_start:
                        completed = True
                        finish_at = now + 1
                    match = re.search(r'^BENCH IDLE .*joint=fresh .*replies=(\d+)', line)
                    if match and int(match[1]) >= 3 and not run_sent:
                        if not opts.run:
                            completed = True
                            record('HOST: read-only verification passed; no motion requested.')
                            break
                        record('HOST: starting authorized three-cycle test at 5deg/s, 1s moves, 2s pauses.')
                        port.write(b'ping\nrun\n')
                        run_sent = True
                        deadline = now + 23
                if finish_at is not None and now >= finish_at:
                    break
            if not completed:
                record('HOST: test did not complete; requesting stop.')
        finally:
            # Also handles Ctrl+C, exceptions and the host-side deadline.
            port.write(b'stop\n')
            end = time.monotonic() + 1
            while time.monotonic() < end:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if line:
                    record(line)
            record(f'HOST: completed={completed}; run_sent={run_sent}; port closing.')
    return 0 if completed else 2


if __name__ == '__main__':
    raise SystemExit(main())
