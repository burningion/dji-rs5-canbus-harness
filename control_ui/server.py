#!/usr/bin/env python3
"""Local browser -> USB serial -> RS5. Run with --demo for no-hardware preview."""
import argparse
import asyncio
from collections import deque
from contextlib import suppress
import json
import math
from pathlib import Path
import secrets
import sys
import time

from aiohttp import web, WSMsgType
import serial
from serial.tools import list_ports

from bridge import Bridge, ControlError

STATIC = Path(__file__).parent / "static"


class DemoDevice:
    """UI simulator, never opens serial or sends CAN."""
    def __init__(self):
        self.start = self.last = self.last_drive = time.monotonic()
        self.token = 1
        self.armed = self.probing = False
        self.yaw = self.pitch = self.pan = self.tilt = 0.0
        self.reason = "idle"
        self.following = self.uwb_configured = False
        self.uwb_tag = 0
        self.uwb_direction, self.uwb_zero, self.uwb_max = 1, 0, 5
        self.tag_visible = True
        self.bearing = 15.0

    def send(self, line):
        args = line.split()
        if args[0] in ("stop", "idle", "probe"):
            self.armed = False
            self.following = False
            self.pan = self.tilt = 0
            self.token += 1
            self.reason = "operator_stop"
            if args[0] != "stop":
                self.probing = args[0] == "probe"
        elif args[0] == "uwb" and int(args[1]) == self.token:
            self.send("stop")
            self.uwb_tag, self.uwb_direction = int(args[3]), int(args[4])
            self.uwb_zero, self.uwb_max = int(args[5])/10, int(args[6])/10
            self.uwb_configured = True
        elif args[0] in ("arm", "track") and int(args[1]) == self.token:
            self.armed = True
            self.following = args[0] == "track"
            self.last_drive = time.monotonic()
            self.reason = "enabled"
        elif args[0] == "drive" and self.armed and int(args[1]) == self.token:
            self.pan, self.tilt = int(args[4])/10, int(args[5])/10
            self.last_drive = time.monotonic()
        elif args[0] == "follow" and self.armed and self.following and int(args[1]) == self.token:
            self.last_drive = time.monotonic()

    def status(self):
        now = time.monotonic()
        dt, self.last = min(now - self.last, .1), now
        if self.armed and now - self.last_drive > .3:
            self.send("stop")
            self.reason = "input_timeout"
        uwb_fresh = self.uwb_configured and self.uwb_tag == 0x1234 and self.tag_visible
        if self.following and not uwb_fresh:
            self.send("stop")
            self.reason = "uwb_lost"
        if self.following:
            error = self.bearing - self.uwb_zero
            target = math.copysign(min(self.uwb_max, max(0, abs(error)-3)), error) * self.uwb_direction
            self.pan += max(-45*dt, min(45*dt, target-self.pan))
            self.tilt = 0
            self.bearing -= self.pan * dt
        self.yaw = (self.yaw - self.pan * dt + 180) % 360 - 180
        self.pitch += self.tilt * dt
        return json.dumps(dict(type="device", protocol=Bridge.PROTOCOL, max_speed_dps=Bridge.MAX_SPEED,
            clock=int((now-self.start)*1000),
            token=self.token, armed=self.armed, can_ready=True, fresh=self.probing,
            ready=self.probing, probing=self.probing, yaw=round(self.yaw, 1), pitch=round(self.pitch, 1),
            roll=0, pan_speed=self.pan, tilt_speed=self.tilt, replies=int((now-self.start)*10),
            reason=self.reason, bus_errors=0, rx_missed=0, fifo_overrun=0, rec=0, tec=0,
            uwb=dict(configured=self.uwb_configured, tag=self.uwb_tag, observed=self.tag_visible,
                observed_tag=0x1234, fresh=uwb_fresh, following=self.following,
                bearing=round(self.bearing, 1), range_m=3.0, age_ms=0 if uwb_fresh else 4294967295,
                reports=int((now-self.start)*10), bad=0, direction=self.uwb_direction,
                zero=self.uwb_zero, max_speed=self.uwb_max,
                reason="tracking" if uwb_fresh else "waiting for tag")))


class Controller:
    def __init__(self, demo=False):
        self.demo = demo
        self.device = None
        self.port = None
        self.owner = None
        self.buffer = bytearray()
        self.events = deque(maxlen=60)
        self.bridge = Bridge(self.send)

    def event(self, message):
        self.events.append({"time": time.strftime("%H:%M:%S"), "message": message})
        print(message, flush=True)

    def ports(self):
        if self.demo:
            return [{"device": "demo", "label": "Simulated Feather · no hardware"}]
        return [{"device": p.device, "label": f"{p.description} · {p.device}"}
                for p in list_ports.comports() if p.vid is not None]

    def send(self, line):
        if self.device is None:
            return
        if self.demo:
            self.device.send(line)
            return
        # A just-written setup/stop line can still be draining normally. Give it
        # at most 20ms; a persistent queue is cleared, never replayed on recovery.
        deadline = time.monotonic() + .020
        while self.device.out_waiting:
            if time.monotonic() >= deadline:
                self.device.reset_output_buffer()
                self.device.write(b"stop\n")
                raise ControlError("USB output congested; reconnect after checking the cable.")
            time.sleep(.001)
        payload = (line + "\n").encode("ascii")
        if self.device.write(payload) != len(payload):
            raise ControlError("Incomplete USB write")

    def connect(self, port):
        if port not in [p["device"] for p in self.ports()]:
            raise ControlError("Choose a connected USB device.")
        self.disconnect()
        self.device = DemoDevice() if self.demo else serial.Serial(port, 115200, timeout=0, write_timeout=.03)
        self.port = port
        self.buffer.clear()
        if not self.demo:
            self.device.reset_input_buffer()
        self.send("stop")
        self.send("probe")
        self.bridge.reason = "Reading joint angles…"
        self.event(f"Connected to {port}; read-only joint queries started.")

    def disconnect(self):
        self.bridge.enabled = self.bridge.awaiting_arm = False
        if self.device:
            with suppress(Exception):
                self.send("idle")
            if not self.demo:
                with suppress(Exception):
                    self.device.close()
            self.device = None
        self.port = None
        self.buffer.clear()
        self.bridge.reset()

    def read(self):
        if not self.device:
            return
        if self.demo:
            self.bridge.accept_device(self.device.status())
            return
        self.buffer.extend(self.device.read(8192))
        if len(self.buffer) > 32768:
            raise ControlError("USB input overflow")
        while b"\n" in self.buffer:
            line, _, self.buffer = self.buffer.partition(b"\n")
            text = line.decode("utf-8", errors="replace").strip()
            if not self.bridge.accept_device(text) and text and not text.startswith("{"):
                self.event(text[:250])

    def snapshot(self):
        return {"type": "state", **self.bridge.snapshot(), "connected": self.device is not None,
                "port": self.port, "demo": self.demo, "events": list(self.events)}


def create_app(demo=False, port=8765):
    controller = Controller(demo)
    allowed_hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
    allowed_origins = {f"http://{host}" for host in allowed_hosts}

    @web.middleware
    async def local_only(request, handler):
        if request.host not in allowed_hosts:
            raise web.HTTPForbidden(text="Use the localhost address printed by the server.")
        response = await handler(request)
        response.headers.update({"X-Content-Type-Options": "nosniff", "Cache-Control": "no-store",
            "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; "
                "connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'"})
        return response

    app = web.Application(middlewares=[local_only], client_max_size=4096)
    app["controller"] = controller

    async def index(request):
        return web.FileResponse(STATIC / "index.html")

    async def asset(request):
        name = request.match_info["name"]
        if name not in {"app.js", "input.mjs", "style.css"}:
            raise web.HTTPNotFound()
        return web.FileResponse(STATIC / name)

    async def ports(request):
        return web.json_response(controller.ports())

    async def status(request):
        # Read-only diagnostics do not claim the control socket or touch USB.
        return web.json_response(controller.snapshot())

    async def websocket(request):
        if request.headers.get("Origin") not in allowed_origins:
            raise web.HTTPForbidden(text="Open this UI on localhost.")
        ws = web.WebSocketResponse(max_msg_size=2048, heartbeat=2)
        await ws.prepare(request)
        if controller.owner is not None:
            await ws.send_json({"type": "error", "message": "Another tab owns control. Close it first."})
            await ws.close()
            return ws
        controller.owner = ws
        tickets = deque(maxlen=8)

        async def publish():
            while not ws.closed:
                ticket = secrets.token_hex(12)
                tickets.append((ticket, time.monotonic()))
                await ws.send_json({**controller.snapshot(), "ticket": ticket})
                await asyncio.sleep(.05)

        publisher = asyncio.create_task(publish())
        try:
            async for msg in ws:
                if msg.type != WSMsgType.TEXT:
                    break
                try:
                    data = json.loads(msg.data)
                    if not isinstance(data, dict):
                        raise ControlError("Invalid input")
                    action = data.get("type")
                    if action == "connect":
                        controller.connect(data.get("port"))
                    elif action == "disconnect":
                        controller.disconnect()
                    elif action == "stop":
                        controller.bridge.stop("Stopped by operator")
                    elif action == "input":
                        current = any(data.get("ticket") == t and time.monotonic()-at <= .25 for t, at in tickets)
                        controller.bridge.input(data, current)
                    elif action == "uwb_config":
                        current = any(data.get("ticket") == t and time.monotonic()-at <= .25 for t, at in tickets)
                        controller.bridge.configure_uwb(data, current)
                    else:
                        raise ControlError("Unknown UI message")
                except (ControlError, ValueError, TypeError) as exc:
                    controller.bridge.stop(str(exc))
                    await ws.send_json({"type": "error", "message": str(exc)})
                except (serial.SerialException, OSError) as exc:
                    controller.disconnect()
                    controller.event(f"USB disconnected: {exc}")
                    await ws.send_json({"type": "error", "message": "USB connection lost. Reconnect to continue."})
        finally:
            publisher.cancel()
            with suppress(asyncio.CancelledError, ConnectionError):
                await publisher
            controller.disconnect()
            controller.owner = None
        return ws

    async def poll():
        while True:
            try:
                controller.read()
                controller.bridge.tick()
            except (serial.SerialException, OSError, ControlError) as exc:
                controller.disconnect()
                controller.event(f"Connection stopped: {exc}")
            await asyncio.sleep(.005)

    async def lifespan(app):
        task = asyncio.create_task(poll())
        yield
        task.cancel()
        with suppress(asyncio.CancelledError):
            await task
        controller.disconnect()

    app.cleanup_ctx.append(lifespan)
    app.router.add_get("/", index)
    app.router.add_get("/api/ports", ports)
    app.router.add_get("/api/status", status)
    app.router.add_get("/ws", websocket)
    app.router.add_get("/{name}", asset)
    return app


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765, help="Local web port (default 8765)")
    parser.add_argument("--demo", action="store_true", help="Simulate angles; never open hardware")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("Choose an unprivileged TCP port between 1024 and 65535")
    print(f"RS5 {'DEMO — no hardware' if args.demo else 'local control'}: http://127.0.0.1:{args.port}")
    web.run_app(create_app(args.demo, args.port), host="127.0.0.1", port=args.port, access_log=None)
