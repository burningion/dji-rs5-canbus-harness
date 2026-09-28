import asyncio
from pathlib import Path
import sys
import unittest

from aiohttp import ClientSession, WSServerHandshakeError, web

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import create_app


class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        # The allowlist uses this logical Host even though the test binds an ephemeral port.
        self.app = create_app(demo=True, port=8766)
        self.runner = web.AppRunner(self.app)
        await self.runner.setup()
        self.site = web.TCPSite(self.runner, '127.0.0.1', 0)
        await self.site.start()
        self.url = f"http://127.0.0.1:{self.site._server.sockets[0].getsockname()[1]}"
        self.session = ClientSession(headers={'Host':'127.0.0.1:8766'})
        self.ws = None
        self.input_seq = 0

    async def asyncTearDown(self):
        if self.ws: await self.ws.close()
        await self.session.close()
        await self.runner.cleanup()

    async def state(self, predicate=lambda s: True):
        async with asyncio.timeout(3):
            while True:
                data = await self.ws.receive_json()
                if data.get('type') == 'state' and predicate(data): return data

    async def connect(self):
        self.ws = await self.session.ws_connect(self.url + '/ws', origin='http://127.0.0.1:8766')
        await self.ws.send_json({'type':'connect', 'port':'demo'})
        return await self.state(lambda s: s['ready'])

    async def input(self, state, held=False, pan=0, tilt=0, source='mouse'):
        self.input_seq += 1
        await self.ws.send_json({'type':'input', 'seq':self.input_seq, 'ticket':state['ticket'],
                                'held':held, 'source':source if held else None, 'pan':pan, 'tilt':tilt})

    async def begin_hold(self, state, pan, tilt=0):
        await self.input(state)
        state = await self.state(lambda s: not s['release_required'])
        await self.input(state, True, pan, tilt)
        return await self.state(lambda s: s['enabled'] and s['device']['armed'])

    async def test_assets_and_local_access(self):
        for path in ['/', '/app.js', '/input.mjs', '/style.css', '/api/ports', '/api/status']:
            async with self.session.get(self.url + path) as response:
                self.assertEqual(response.status, 200)
                self.assertGreater(len(await response.read()), 20)
        async with self.session.get(self.url + '/', headers={'Host':'evil.example:8766'}) as response:
            self.assertEqual(response.status, 403)
        with self.assertRaises(WSServerHandshakeError):
            await self.session.ws_connect(self.url + '/ws', origin='http://evil.example')

    async def test_proportional_motion_release_and_lease(self):
        state = await self.connect()
        self.assertFalse(state['enabled'])
        self.assertEqual(state['device']['pan_speed'], 0)
        state = await self.begin_hold(state, 2.5, -1)
        for seq in range(1, 6):
            await self.input(state, True, 2.5, -1)
            state = await self.state()
        state = await self.state(lambda s: s['device']['pan_speed'] == 2.5)
        self.assertLess(state['device']['yaw'], 0)
        self.assertLess(state['device']['pitch'], 0)
        await self.input(state, True, 0, 0)
        state = await self.state(lambda s: s['device']['pan_speed'] == 0)
        self.assertEqual(state['device']['tilt_speed'], 0)
        # A vanished browser causes stop with no new commands.
        state = await self.state(lambda s: not s['enabled'])
        self.assertIn('timed out', state['reason'])
        # The device acknowledgement arrives on the next telemetry sample.
        async with asyncio.timeout(.2):
            state = await self.state(lambda s: not s['enabled'] and not s['device']['armed'])
        self.assertFalse(state['device']['armed'])
        self.assertEqual(state['device']['pan_speed'], 0)
        self.assertEqual(state['device']['tilt_speed'], 0)

    async def test_full_speed_pan_wrap_does_not_stop(self):
        state = await self.connect()
        self.app['controller'].device.yaw = 179
        self.app['controller'].device.pitch = 190
        state = await self.state(lambda s: s['device']['yaw'] == 179)
        state = await self.begin_hold(state, -60, 60)
        for seq in range(1, 6):
            await self.input(state, True, -60, 60)
            state = await self.state()
        state = await self.state(lambda s: s['device']['yaw'] < 0 and s['device']['pitch'] > 190)
        self.assertTrue(state['enabled'])
        self.assertEqual(state['device']['pan_speed'], -60)
        self.assertEqual(state['device']['tilt_speed'], 60)
        await self.input(state)
        state = await self.state(lambda s: not s['enabled'] and not s['device']['armed'])
        self.assertEqual(state['device']['pan_speed'], 0)

    async def test_single_owner_and_socket_loss(self):
        await self.connect()
        # Observing diagnostics must not steal ownership or close the device.
        async with self.session.get(self.url + '/api/status') as response:
            snapshot = await response.json()
            self.assertTrue(snapshot['connected'])
            self.assertFalse(snapshot['enabled'])
        other = await self.session.ws_connect(self.url + '/ws', origin='http://127.0.0.1:8766')
        self.assertIn('Another tab', (await other.receive_json())['message'])
        await other.close()
        await self.ws.close()
        for _ in range(20):
            if self.app['controller'].device is None: break
            await asyncio.sleep(.01)
        self.assertIsNone(self.app['controller'].device)
        self.assertFalse(self.app['controller'].bridge.enabled)

    async def test_expired_ticket_cannot_start_hold(self):
        state = await self.connect()
        await asyncio.sleep(.3)
        await self.input(state, True, 30)
        async with asyncio.timeout(2):
            while True:
                message = await self.ws.receive_json()
                if message['type'] == 'error': break
        self.assertFalse(self.app['controller'].bridge.enabled)


if __name__ == '__main__': unittest.main()
