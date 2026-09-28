#!/usr/bin/env python3
"""Verify the running UI bridge against real telemetry. Never enables movement."""
import argparse
import asyncio
import json
from pathlib import Path
import time
from aiohttp import ClientSession


async def check(url, port, log, seconds=0):
    states = []
    async with ClientSession() as session:
        async with session.ws_connect(url + '/ws', origin=url) as ws:
            try:
                await ws.send_json({'type': 'connect', 'port': port})
                began = last_report = time.monotonic()
                async with asyncio.timeout(max(10, seconds + 10)):
                    while True:
                        state = await ws.receive_json()
                        if state.get('type') == 'error': raise RuntimeError(state['message'])
                        if state.get('type') != 'state': continue
                        device = state.get('device')
                        if device:
                            states.append(device)
                            if device['armed']: raise RuntimeError('Device unexpectedly armed')
                            if device['pan_speed'] or device['tilt_speed']:
                                raise RuntimeError('Unexpected motion request')
                            if not device['can_ready']: raise RuntimeError('CAN fault: ' + device['reason'])
                            elapsed = time.monotonic() - began
                            if seconds and time.monotonic() - last_report >= 10:
                                print(f"{elapsed:.0f}s: replies={device['replies']} errors={device['bus_errors']} "
                                      f"arbitration_waits={device.get('arbitration_lost', 'n/a')} armed={device['armed']}", flush=True)
                                last_report = time.monotonic()
                            if state['ready'] and device['replies'] >= 15 and elapsed >= seconds:
                                print(json.dumps(device, indent=2))
                                print('PASS: live pan/tilt/roll replies, movement stayed disabled.')
                                break
            finally:
                await ws.send_json({'type': 'disconnect'})
                if log:
                    Path(log).write_text('\n'.join(json.dumps(s) for s in states) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8765')
    parser.add_argument('--port', required=True, help='Feather USB serial port')
    parser.add_argument('--log')
    parser.add_argument('--seconds', type=float, default=0, help='Minimum read-only observation duration')
    args = parser.parse_args()
    if not 0 <= args.seconds <= 300: parser.error('--seconds must be between 0 and 300')
    asyncio.run(check(args.url.rstrip('/'), args.port, args.log, args.seconds))
