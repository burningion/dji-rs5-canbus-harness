import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bridge import Bridge, ControlError


def device(**overrides):
    return json.dumps(dict(type="device", protocol=2, max_speed_dps=60, clock=100, token=42, armed=False,
        can_ready=True, fresh=True, ready=True, probing=True, yaw=2, roll=1, pitch=0,
        pan_speed=0, tilt_speed=0, replies=3, reason="idle", bus_errors=0, rx_missed=0,
        fifo_overrun=0, rec=0, tec=0) | overrides)


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.now = 1.0
        self.commands = []
        self.bridge = Bridge(self.commands.append, lambda: self.now)
        self.bridge.accept_device(device())

    def arm(self):
        self.bridge.arm()
        self.bridge.accept_device(device(armed=True))

    def test_never_moves_on_connect_or_arm(self):
        self.assertEqual(self.commands, [])
        self.bridge.arm()
        self.assertEqual(self.commands, ["arm 42 100"])
        self.bridge.drive(dict(seq=1, pan=10, tilt=-2.5), True)
        self.assertEqual(len(self.commands), 1)  # Waits for device acknowledgement.

    def test_proportional_input_and_release(self):
        self.arm()
        self.bridge.drive(dict(seq=1, pan=2.4, tilt=-.3), True)
        self.assertEqual(self.commands[-1], "drive 42 100 1 24 -3")
        self.bridge.drive(dict(seq=2, pan=0, tilt=0), True)
        self.assertTrue(self.commands[-1].endswith(" 0 0"))

    def test_no_cached_replay(self):
        self.arm()
        self.bridge.drive(dict(seq=1, pan=10, tilt=10), True)
        for i in range(1, 5):
            self.now = 1 + i * .05
            self.bridge.accept_device(device(armed=True))
            self.bridge.tick()
        self.assertEqual(len(self.commands), 2)
        self.now = 1.251
        self.bridge.tick()
        self.assertEqual(self.commands[-1], "stop")
        self.assertFalse(self.bridge.enabled)

    def test_late_input_cannot_revive(self):
        self.arm()
        self.now += .251
        self.bridge.accept_device(device(armed=True))
        with self.assertRaises(ControlError):
            self.bridge.drive(dict(seq=1, pan=1, tilt=0), True)
        self.assertFalse(self.bridge.enabled)

    def test_bad_inputs_disarm(self):
        for payload, ticket in [(dict(seq=1, pan=60.1, tilt=0), True),
            (dict(seq=1, pan=0, tilt=-60.1), True),
            (dict(seq=1, pan=0, tilt=float('nan')), True),
            (dict(seq=True, pan=0, tilt=0), True),
            (dict(seq=1, pan=1, tilt=1), False), (dict(seq=0, pan=0, tilt=0), True)]:
            with self.subTest(payload=payload):
                self.setUp(); self.arm()
                with self.assertRaises(ControlError): self.bridge.drive(payload, ticket)
                self.assertFalse(self.bridge.enabled)
                self.assertEqual(self.commands[-1], "stop")

    def test_duplicate_input_disarms(self):
        self.arm()
        self.bridge.drive(dict(seq=1, pan=1, tilt=0), True)
        with self.assertRaises(ControlError): self.bridge.drive(dict(seq=1, pan=1, tilt=0), True)
        self.assertFalse(self.bridge.enabled)

    def test_device_stop_and_stale_telemetry(self):
        self.arm()
        self.bridge.accept_device(device(token=43, reason="telemetry_stale"))
        self.assertFalse(self.bridge.enabled)
        self.assertEqual(self.commands[-1], "stop")
        self.now += .251
        with self.assertRaises(ControlError): self.bridge.arm()

    def test_wrong_firmware_and_corruption(self):
        self.bridge.reset()
        for line in ['BENCH IDLE', '{}', '[]', device(protocol=1), device(protocol=3),
                     device(max_speed_dps=10), device(yaw=float('inf')), device(token=-1)]:
            self.assertFalse(self.bridge.accept_device(line))
        with self.assertRaises(ControlError): self.bridge.arm()

    def test_stale_or_unowned_device_does_not_enable(self):
        self.bridge.accept_device(device(armed=True))
        self.assertEqual(self.commands[-1], 'stop')
        self.assertFalse(self.bridge.enabled)

    def test_full_speed_across_pan_seam_and_outside_old_limits(self):
        self.bridge.accept_device(device(yaw=179.9, pitch=214, roll=240))
        self.arm()
        for seq, yaw in enumerate([179.9, -180, -179.9, -170, 360], start=1):
            self.now += .05
            self.bridge.accept_device(device(armed=True, yaw=yaw, pitch=214, roll=240))
            self.bridge.drive(dict(seq=seq, pan=60, tilt=-60), True)
            self.assertEqual(self.commands[-1], f"drive 42 100 {seq} 600 -600")
            self.assertTrue(self.bridge.enabled)
        self.bridge.drive(dict(seq=6, pan=0, tilt=0), True)
        self.assertTrue(self.commands[-1].endswith(' 0 0'))

    def hold(self, seq, held=True, source='mouse', pan=30, tilt=0):
        self.bridge.input(dict(seq=seq, held=held, source=source if held else None,
                               pan=pan if held else 0, tilt=tilt if held else 0), True)

    def test_hold_starts_and_release_stops_without_manual_enable(self):
        self.hold(1)
        self.assertEqual(self.commands, [])  # Must observe release after connect.
        self.hold(2, False)
        self.hold(3)
        self.assertEqual(self.commands, ['arm 42 100'])
        self.bridge.accept_device(device(armed=True))
        self.hold(4)
        self.assertTrue(self.commands[-1].endswith(' 300 0'))
        self.hold(5, False)
        self.assertEqual(self.commands[-1], 'stop')
        self.assertFalse(self.bridge.enabled)
        self.assertTrue(self.bridge.release_required)
        self.bridge.accept_device(device(token=43))
        self.hold(6, False)
        self.hold(7, source='gamepad', pan=-60)
        self.assertEqual(self.commands[-1], 'arm 43 100')

    def test_hold_cannot_restart_after_input_timeout_or_switch_sources(self):
        self.hold(1, False); self.hold(2)
        self.bridge.accept_device(device(armed=True))
        self.hold(3)
        self.now += .251
        self.bridge.accept_device(device(armed=True))
        self.bridge.tick()
        self.assertFalse(self.bridge.enabled)
        self.bridge.accept_device(device(token=43))
        count = len(self.commands)
        self.hold(4)
        self.assertEqual(len(self.commands), count)  # Still held: no automatic restart.
        self.hold(5, False); self.hold(6)
        self.bridge.accept_device(device(token=43, armed=True))
        self.hold(7, source='gamepad')
        self.assertFalse(self.bridge.enabled)
        self.assertTrue(self.bridge.release_required)

    def test_release_messages_need_valid_shape_and_freshness(self):
        for message, ticket in [(dict(seq=1, held=False, source=None, pan=1, tilt=0), True),
            (dict(seq=1, held=True, source=None, pan=1, tilt=0), True),
            (dict(seq=1, held=False, source=None, pan=0, tilt=0), False)]:
            with self.subTest(message=message):
                self.setUp()
                with self.assertRaises(ControlError): self.bridge.input(message, ticket)
                self.assertTrue(self.bridge.release_required)


if __name__ == '__main__': unittest.main()
