// Pure input mapping; shared by browser and Node tests.
export function connectionHint(state) {
  if (!state.connected) return 'Choose your USB device to read live angles.';
  if (!state.device) return 'USB open; waiting for the Feather firmware to respond…';
  if (!state.device.can_ready) return 'CAN connection stopped. Check RS5 power and wiring, then reset the Feather.';
  if (state.telemetry_age_ms > 250) return 'Feather stopped responding over USB. Disconnect and reconnect.';
  if (!state.device.probing) return 'Feather connected; angle queries are stopped. Disconnect and reconnect.';
  if (!state.ready) return 'Feather ready; waiting for RS5 angles. Check that the gimbal is powered on.';
  return 'Live angles received. Ready when you are.';
}

export function shapeStick(x, y, deadzone = 0.12, exponent = 1.6) {
  if (![x, y].every(Number.isFinite)) return { x: 0, y: 0 };
  const radius = Math.hypot(x, y);
  if (radius <= deadzone) return { x: 0, y: 0 };
  const gain = ((Math.min(radius, 1) - deadzone) / (1 - deadzone)) ** exponent;
  return { x: (x / radius) * gain, y: (y / radius) * gain };
}

export function gamepadInput(pad) {
  const supported = !!pad && pad.connected && pad.mapping === 'standard' &&
    pad.axes?.length >= 2 && pad.buttons?.length >= 6 &&
    Number.isFinite(pad.axes[0]) && Number.isFinite(pad.axes[1]);
  if (!supported) return { supported: false, neutral: false, held: false, stop: false, follow: false, x: 0, y: 0 };
  const held = !!pad.buttons[4].pressed; // L1 on a standard-mapped DualShock 4.
  const stick = shapeStick(pad.axes[0], pad.axes[1]);
  return { supported: true, held, stop: !!pad.buttons[1].pressed, follow: !!pad.buttons[0].pressed,
    neutral: !held && stick.x === 0 && stick.y === 0,
    x: held ? stick.x : 0, y: held ? stick.y : 0 };
}

// X must be released while available before a press can toggle tracking.
export class PressEdge {
  constructor() { this.reset(); }
  reset() { this.released = false; }
  sample(pressed, available) {
    if (!available) { this.reset(); return false; }
    if (!pressed) { this.released = true; return false; }
    const edge = this.released;
    this.released = false;
    return edge;
  }
}

export function speeds(vector, maxSpeed, invertPan, invertTilt) {
  if (!Number.isFinite(maxSpeed)) return { pan: 0, tilt: 0 };
  const max = Math.max(0, Math.min(60, maxSpeed));
  return { pan: Math.round(vector.x * max * (invertPan ? -1 : 1) * 10) / 10,
    tilt: Math.round(-vector.y * max * (invertTilt ? -1 : 1) * 10) / 10 };
}

// The first held source owns the gesture. A still-held second source cannot
// take over after release, a disconnect, or a fault; release both to continue.
export class InputRouter {
  constructor() { this.source = null; this.blocked = true; this.padReleased = false; }
  reset() { this.source = null; this.blocked = true; this.padReleased = false; }
  step(mouse, pad, available) {
    const idle = { held: false, source: null, x: 0, y: 0 };
    const padHeld = !!pad.supported && !!pad.held;
    if (!pad.supported) this.padReleased = false;
    else if (!pad.held) this.padReleased = true;
    if (!available || pad.stop) { this.reset(); return idle; }
    if (this.blocked) {
      if (!mouse.held && !padHeld) this.blocked = false;
      return idle;
    }
    if ((this.source === 'mouse' && !mouse.held) || (this.source === 'gamepad' && !padHeld)) {
      this.source = null;
      this.blocked = !!mouse.held || padHeld;
      return idle;
    }
    if (!this.source) this.source = mouse.held ? 'mouse' : padHeld && this.padReleased ? 'gamepad' : null;
    if (!this.source) return idle;
    const input = this.source === 'mouse' ? mouse : pad;
    return { held: true, source: this.source, x: input.x, y: input.y };
  }
}
