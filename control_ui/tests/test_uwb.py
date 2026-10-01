import json
import unittest
from test_bridge import device
from bridge import Bridge, ControlError


def uwb(**overrides):
    return dict(configured=True, tag=0x1234, observed=True, observed_tag=0x1234,
                fresh=True, following=False, bearing=12.0, range_m=3.0,
                zero=0, max_speed=5, direction=1) | overrides


class UwbTests(unittest.TestCase):
    def setUp(self):
        self.now = 1.0
        self.commands = []
        self.bridge = Bridge(self.commands.append, lambda: self.now)
        self.bridge.accept_device(device(uwb=uwb()))
        self.seq = 0

    def input(self, source=None, pan=0, ticket=True):
        self.seq += 1
        self.bridge.input(dict(seq=self.seq, held=source is not None, source=source, pan=pan, tilt=0), ticket)

    def start(self):
        self.input()
        self.input('uwb')
        self.assertEqual(self.commands[-1], 'track 42 100')
        self.bridge.accept_device(device(armed=True, uwb=uwb(following=True)))
        self.assertFalse(self.bridge.awaiting_arm)

    def test_follow_uses_lease_without_host_speed_commands(self):
        self.start()
        self.input('uwb')
        self.assertEqual(self.commands[-1], 'follow 42 100 1')
        self.assertFalse(any(c.startswith('drive') for c in self.commands))
        self.input()
        self.assertEqual(self.commands[-1], 'stop')
        self.assertFalse(self.bridge.enabled)

    def test_lost_fix_requires_release_and_explicit_new_follow(self):
        self.start()
        self.bridge.accept_device(device(armed=True, uwb=uwb(following=True, fresh=False)))
        self.bridge.tick()
        self.assertFalse(self.bridge.enabled)
        self.bridge.accept_device(device(token=43, uwb=uwb()))
        self.input('uwb')
        self.assertFalse(self.bridge.enabled)
        self.input()
        self.input('uwb')
        self.assertEqual(self.commands[-1], 'track 43 100')

    def test_disappearance_never_synthesizes_follow_heartbeat(self):
        self.start()
        count = len(self.commands)
        self.now += .1
        self.bridge.tick()
        self.assertEqual(len(self.commands), count)
        self.now += .151
        self.bridge.tick()
        self.assertFalse(self.bridge.enabled)
        self.assertEqual(self.commands[-1], 'stop')

    def test_manual_takeover_stops_and_requires_release(self):
        self.start()
        self.input('gamepad', 20)
        self.assertFalse(self.bridge.enabled)
        self.assertTrue(self.bridge.release_required)
        self.assertEqual(self.commands[-1], 'stop')

    def test_wrong_mode_acknowledgement_stops(self):
        self.input()
        self.input('uwb')
        self.bridge.accept_device(device(armed=True, uwb=uwb(following=False)))
        self.assertFalse(self.bridge.enabled)

    def test_old_firmware_remains_manual_only(self):
        self.bridge.accept_device(device())
        self.input()
        with self.assertRaises(ControlError): self.input('uwb')
        self.assertFalse(self.bridge.enabled)
        self.input('mouse', 10)
        self.assertEqual(self.commands[-1], 'arm 42 100')

    def test_invalid_or_replayed_follow_input_stops(self):
        self.start()
        with self.assertRaises(ControlError): self.input('uwb', 1)
        self.assertFalse(self.bridge.enabled)
        self.setUp(); self.start()
        with self.assertRaises(ControlError): self.input('uwb', ticket=False)
        self.assertFalse(self.bridge.enabled)

    def test_configuration_bounds_and_freshness(self):
        config = dict(tag='ABCD', direction=-1, zero=2.5, max_speed=5)
        self.bridge.configure_uwb(config, True)
        self.assertEqual(self.commands[-1], 'uwb 42 100 43981 -1 25 50')
        self.assertFalse(self.bridge.enabled)
        for changes in [dict(tag='12345'), dict(tag='12XZ'), dict(direction=True),
                        dict(zero=float('nan')), dict(max_speed=16), dict(zero=31)]:
            with self.assertRaises(ControlError): self.bridge.configure_uwb(config | changes, True)
        with self.assertRaises(ControlError): self.bridge.configure_uwb(config, False)
        self.start()
        with self.assertRaises(ControlError): self.bridge.configure_uwb(config, True)

    def test_invalid_uwb_telemetry_cannot_refresh_connection(self):
        for changes in [dict(tag=65536), dict(fresh='true'), dict(bearing=float('nan')), dict(direction=0)]:
            self.assertFalse(self.bridge.accept_device(device(uwb=uwb(**changes))))
        self.now += .251
        self.assertFalse(self.bridge.uwb_ready())


if __name__ == '__main__': unittest.main()
