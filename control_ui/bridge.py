"""One owner, bounded fresh input, and no cached motion replay."""
import json
import math
import time


class ControlError(ValueError):
    pass


class Bridge:
    PROTOCOL = 2
    MAX_SPEED = 60
    INPUT_TIMEOUT = 0.250
    DEVICE_TIMEOUT = 0.250

    def __init__(self, send, clock=time.monotonic):
        self.send = send
        self.clock = clock
        self.device = None
        self.device_at = 0
        self.enabled = False
        self.last_input = 0
        self.last_client_seq = 0
        self.serial_seq = 0
        self.arm_token = None
        self.awaiting_arm = False
        self.arm_at = 0
        self.input_sequence = 0
        self.release_required = True
        self.source = None
        self.reason = "Connect your Feather to begin."

    def reset(self):
        self.enabled = self.awaiting_arm = False
        self.device = None
        self.arm_token = None
        self.input_sequence = 0
        self.release_required = True
        self.source = None
        self.reason = "Disconnected"

    def accept_device(self, line):
        try:
            data = json.loads(line)
        except (ValueError, TypeError):
            return False
        if (not isinstance(data, dict) or data.get("type") != "device" or
                data.get("protocol") != self.PROTOCOL or data.get("max_speed_dps") != self.MAX_SPEED):
            return False
        for key in ("clock", "token", "replies", "bus_errors", "rx_missed", "fifo_overrun", "rec", "tec"):
            if type(data.get(key)) is not int or not 0 <= data[key] <= 0xFFFFFFFF:
                return False
        for key in ("yaw", "roll", "pitch", "pan_speed", "tilt_speed"):
            if type(data.get(key)) not in (int, float) or not math.isfinite(data[key]):
                return False
        for key in ("armed", "can_ready", "fresh", "ready", "probing"):
            if type(data.get(key)) is not bool:
                return False
        if "uwb" in data:
            u = data["uwb"]
            if not isinstance(u, dict):
                return False
            if any(type(u.get(k)) is not bool for k in ("configured", "observed", "fresh", "following")):
                return False
            if any(type(u.get(k)) is not int or not 0 <= u[k] <= 65535 for k in ("tag", "observed_tag")):
                return False
            if any(type(u.get(k)) not in (int, float) or not math.isfinite(u[k])
                   for k in ("bearing", "range_m", "zero", "max_speed")):
                return False
            if type(u.get("direction")) is not int or u["direction"] not in (-1, 1):
                return False
        self.device = data
        self.device_at = self.clock()
        if self.enabled:
            if data["token"] != self.arm_token:
                self.stop(str(data.get("reason", "Device stopped")))
            elif data["armed"]:
                if bool(data.get("uwb", {}).get("following")) != (self.source == "uwb"):
                    self.stop("Device control mode changed")
                else:
                    self.awaiting_arm = False
            elif not self.awaiting_arm:
                self.stop(str(data.get("reason", "Device stopped")))
        elif data["armed"]:
            # Attaching to an already armed device never grants ownership.
            self.stop("Device had no active UI owner")
        return True

    def ready(self):
        return bool(self.device and self.device.get("ready") and
                    self.clock() - self.device_at <= self.DEVICE_TIMEOUT)

    def stop(self, reason="Stopped"):
        self.enabled = self.awaiting_arm = False
        self.arm_token = None
        self.release_required = True
        self.source = None
        self.reason = reason
        self.send("stop")

    def uwb_ready(self):
        u = (self.device or {}).get("uwb", {})
        return bool(self.ready() and u.get("configured") and u.get("fresh"))

    def configure_uwb(self, message, ticket_valid):
        if not ticket_valid or not self.ready() or "uwb" not in self.device:
            raise ControlError("UWB setup needs live angles and UWB-capable firmware.")
        if self.enabled or self.device["armed"]:
            raise ControlError("Stop movement before applying UWB settings.")
        tag, direction = message.get("tag"), message.get("direction")
        zero, maximum = message.get("zero"), message.get("max_speed")
        if (not isinstance(tag, str) or len(tag) != 4 or any(c not in "0123456789abcdefABCDEF" for c in tag)
                or type(direction) is not int or direction not in (-1, 1)
                or type(zero) not in (int, float) or not math.isfinite(zero) or not -30 <= zero <= 30
                or type(maximum) not in (int, float) or not math.isfinite(maximum) or not 1 <= maximum <= 30):
            raise ControlError("Use a four-digit hex tag, direction ±1, center ±30°, and speed 1–30°/s.")
        d = self.device
        self.release_required = True
        self.reason = "UWB settings sent; waiting for three fresh tag reports."
        self.send(f"uwb {d['token']} {d['clock']} {int(tag, 16)} {direction} {round(zero*10)} {round(maximum*10)}")

    def arm(self, source=None):
        if self.enabled or not self.ready() or self.device["armed"]:
            raise ControlError("Wait for live angles, then release and hold the control again.")
        d = self.device
        if source == "uwb" and not self.uwb_ready():
            raise ControlError("Wait for a fresh selected tag before starting UWB follow.")
        self.source = source
        self.arm_token = d["token"]
        self.serial_seq = self.last_client_seq = 0
        self.last_input = self.clock()
        self.arm_at = self.last_input
        self.enabled = self.awaiting_arm = True
        self.reason = "Following UWB tag" if source == "uwb" else "Control held"
        command = "track" if source == "uwb" else "arm"
        self.send(f"{command} {d['token']} {d['clock']}")

    def drive(self, message, ticket_valid):
        if not self.enabled:
            raise ControlError("Release the control, then hold it again.")
        # A late message cannot refresh an expired lease.
        if self.clock() - self.last_input > self.INPUT_TIMEOUT or not self.ready():
            self.stop("Input or telemetry timed out")
            raise ControlError(self.reason)
        seq = message.get("seq")
        values = [message.get("pan"), message.get("tilt")]
        valid = (ticket_valid and type(seq) is int and self.last_client_seq < seq <= 0xFFFFFFFF and
                 all(type(v) in (int, float) and math.isfinite(v) and abs(v) <= self.MAX_SPEED for v in values))
        if not valid:
            self.stop("Invalid or delayed input")
            raise ControlError(self.reason)
        if self.source == "uwb" and (values != [0, 0] or not self.uwb_ready()):
            self.stop("UWB fix lost — press X again after reacquiring the tag")
            raise ControlError(self.reason)
        self.last_client_seq = seq
        self.last_input = self.clock()
        # Do not send drive before the board acknowledges this enable request.
        if self.awaiting_arm:
            return
        self.serial_seq += 1
        d = self.device
        if self.source == "uwb":
            self.send(f"follow {self.arm_token} {d['clock']} {self.serial_seq}")
            return
        self.send(f"drive {self.arm_token} {d['clock']} {self.serial_seq} "
                  f"{round(values[0] * 10)} {round(values[1] * 10)}")

    def tick(self):
        if self.enabled and self.awaiting_arm and self.clock() - self.arm_at > .3:
            self.stop("Device did not respond — release and hold again")
        elif self.enabled and (self.clock() - self.last_input > self.INPUT_TIMEOUT or not self.ready()):
            self.stop("Input or telemetry timed out — release and hold again")
        elif self.enabled and self.source == "uwb" and not self.uwb_ready():
            self.stop("UWB fix lost — press X again after reacquiring the tag")

    def input(self, message, ticket_valid):
        """Fresh held input starts a session; release ends it. Never auto-resume."""
        seq, held, source = message.get("seq"), message.get("held"), message.get("source")
        values = [message.get("pan"), message.get("tilt")]
        valid = (ticket_valid and type(seq) is int and self.input_sequence < seq <= 0xFFFFFFFF
                 and type(held) is bool and source in (None, "mouse", "gamepad", "uwb")
                 and all(type(v) in (int, float) and math.isfinite(v) and abs(v) <= self.MAX_SPEED for v in values)
                 and (held and source is not None or not held and source is None and values == [0, 0])
                 and (source != "uwb" or values == [0, 0]))
        if not valid:
            self.stop("Invalid or delayed input — release the controls")
            raise ControlError(self.reason)
        self.input_sequence = seq
        if not held:
            if self.enabled:
                self.stop("Released")
            # Wait for actual disarm telemetry before allowing another hold.
            if self.ready() and not self.device["armed"]:
                self.release_required = False
                self.reason = "Ready — hold the mouse control or L1 + stick"
            return
        if not self.ready():
            self.stop("Waiting for live angles — release the controls")
            return
        if self.release_required:
            return  # A still-held input after a fault cannot restart movement.
        if self.enabled and source != self.source:
            self.stop("Input changed — release both controls")
            return
        if not self.enabled:
            if self.device["armed"]:
                self.stop("Waiting for the device to stop")
                return
            self.arm(source)
        self.drive(message, ticket_valid)

    def snapshot(self):
        return {"enabled": self.enabled, "ready": self.ready(), "reason": self.reason,
                "release_required": self.release_required, "source": self.source,
                "uwb_ready": self.uwb_ready(),
                "device": self.device, "telemetry_age_ms": round((self.clock() - self.device_at) * 1000)
                if self.device else None}
