from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import Controller
from bridge import ControlError


class FakeSerial:
    def __init__(self, pending, stuck=False):
        self.pending = pending
        self.stuck = stuck
        self.writes = []
        self.cleared = False

    @property
    def out_waiting(self):
        if self.stuck: return 4
        if self.pending:
            self.pending -= 1
            return 4
        return 0

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def reset_output_buffer(self): self.cleared = True


class SerialTests(unittest.TestCase):
    def test_brief_usb_drain_is_not_a_connection_failure(self):
        controller = Controller()
        controller.device = FakeSerial(2)
        controller.send('probe')
        self.assertEqual(controller.device.writes, [b'probe\n'])
        self.assertFalse(controller.device.cleared)

    def test_persistent_backlog_is_cleared_and_speed_is_not_sent(self):
        controller = Controller()
        controller.device = FakeSerial(0, stuck=True)
        with self.assertRaises(ControlError): controller.send('drive 1 1 1 600 0')
        self.assertTrue(controller.device.cleared)
        self.assertEqual(controller.device.writes, [b'stop\n'])


if __name__ == '__main__': unittest.main()
