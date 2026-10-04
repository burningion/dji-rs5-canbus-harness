import { shapeStick, gamepadInput, speeds, connectionHint, InputRouter, PressEdge } from './input.mjs';

const $ = id => document.getElementById(id);
const ui = Object.fromEntries(['port','connect','refresh','connection-label','port-hint','demo-banner',
  'input-note','input-status','pad','puck','pad-lock','yaw','pitch','roll','live-badge','can-health',
  'speed','speed-value','invert-pan','invert-tilt','control-state','enabled-dot','status-message',
  'diagnostic','events','controller-status','active-source','uwb-tag','uwb-direction','uwb-zero',
  'uwb-speed','uwb-apply','uwb-follow','uwb-status','uwb-badge','uwb-bearing','uwb-range',
  'uwb-observed','uwb-settings'].map(id => [id, $(id)]));
const router = new InputRouter();
const cross = new PressEdge();
let following = false, followAcknowledged = false;
let ws, state = {}, ticket = '', lastState = 0, lastSend = 0, seq = 0, previousFrame = 0;
let pointer = null, mouse = { held: false, x: 0, y: 0 }, padIndex = null, padId = '';
let controller = gamepadInput(null), warning = '';
let intent = { held: false, source: null, x: 0, y: 0 };

function send(message) {
  if (ws?.readyState !== WebSocket.OPEN) return false;
  if (ws.bufferedAmount > 1024) { ws.close(); return false; }
  ws.send(JSON.stringify(message));
  return true;
}
function clearMouse() {
  const previousPointer = pointer;
  pointer = null;
  mouse = { held: false, x: 0, y: 0 };
  if (previousPointer !== null) {
    for (const element of [ui.pad, ...document.querySelectorAll('[data-x]')]) {
      if (element.hasPointerCapture(previousPointer)) element.releasePointerCapture(previousPointer);
    }
  }
}
function cancel(reason = '') {
  following = followAcknowledged = false; cross.reset();
  router.reset(); clearMouse();
  intent = { held: false, source: null, x: 0, y: 0 };
  send({ type: 'stop' });
  if (reason) warning = reason;
}
function available() {
  return !!state.connected && !!state.ready && performance.now() - lastState < 250 &&
    document.hasFocus() && !document.hidden;
}
function uwbSettingsMatch() {
  const u = state.device?.uwb;
  return !!u?.configured && /^[0-9a-f]{4}$/i.test(ui['uwb-tag'].value) &&
    ui['uwb-zero'].value !== '' && ui['uwb-speed'].value !== '' &&
    parseInt(ui['uwb-tag'].value, 16) === u.tag && Number(ui['uwb-direction'].value) === u.direction &&
    Number(ui['uwb-zero'].value) === u.zero && Number(ui['uwb-speed'].value) === u.max_speed;
}
function toggleFollow() {
  if (following || state.source === 'uwb') { cancel('UWB follow stopped.'); return; }
  if (!available() || !state.uwb_ready || state.release_required || state.enabled ||
      mouse.held || controller.held || controller.stop || !uwbSettingsMatch()) {
    warning = 'Apply UWB settings, wait for a fresh tag, and release the controls before pressing X.';
    return;
  }
  following = true; followAcknowledged = false; warning = '';
  pump(performance.now(), true);
}
function pump(now, force = false) {
  if (following && (mouse.held || controller.held || controller.stop)) {
    cancel('UWB follow cancelled. Release and hold again for manual control.');
  }
  if (following && (!available() || !state.uwb_ready || state.release_required)) {
    cancel('UWB follow stopped. Wait for a fresh tag, then press X again.');
  }
  const previousSource = router.source;
  intent = router.step(mouse, controller, available());
  if (following) intent = { held: true, source: 'uwb', x: 0, y: 0 };
  // Device/bridge failures require a fresh hold, never replay a held gesture.
  if (state.release_required && intent.held) {
    router.reset(); clearMouse();
    intent = { held: false, source: null, x: 0, y: 0 };
    warning = state.reason || 'Release the controls, then hold again.';
  } else if (!previousSource && intent.source) warning = '';
  if (state.connected && performance.now() - lastState < 250 && (force || now - lastSend >= 50)) {
    const output = intent.held && intent.source !== 'uwb'
      ? speeds(intent, Number(ui.speed.value), ui['invert-pan'].checked, ui['invert-tilt'].checked)
      : { pan: 0, tilt: 0 };
    send({ type: 'input', ticket, seq: ++seq, held: intent.held, source: intent.source, ...output });
    lastSend = now;
  }
}
function render() {
  const d = state.device;
  const fresh = state.connected && d?.fresh && state.telemetry_age_ms <= 250 && performance.now() - lastState < 250;
  const moving = intent.held && !!state.enabled && available();
  ui['demo-banner'].hidden = !state.demo;
  ui['connection-label'].textContent = ws?.readyState !== WebSocket.OPEN ? 'App disconnected'
    : state.connected ? (state.demo ? 'Demo connected' : 'USB connected') : 'USB disconnected';
  $('connection').querySelector('.dot').classList.toggle('active', !!state.connected);
  ui.connect.textContent = state.connected ? 'Disconnect' : 'Connect';
  ui.connect.disabled = ws?.readyState !== WebSocket.OPEN || (!state.connected && !ui.port.value);
  ui.port.disabled = !!state.connected; ui.refresh.disabled = !!state.connected;
  ui['port-hint'].textContent = connectionHint(state);
  ui['port-hint'].classList.toggle('error-text', !!state.connected && d?.can_ready === false);
  for (const axis of ['yaw', 'pitch']) ui[axis].textContent = d ? Number(d[axis]).toFixed(1) : '—';
  ui.roll.textContent = d ? `${Number(d.roll).toFixed(1)}°` : '—';
  ui['live-badge'].textContent = fresh ? '● LIVE' : d ? 'STALE' : 'WAITING';
  ui['live-badge'].classList.toggle('live', fresh);
  ui['can-health'].textContent = d?.can_ready === false ? 'CAN stopped · reset required'
    : d ? `${d.bus_errors} bus errors` : 'No telemetry yet';
  ui['enabled-dot'].classList.toggle('active', moving);
  ui['control-state'].textContent = moving ? (following ? 'Following UWB tag' : 'Following your input') : state.ready ? 'Ready to move' : 'Waiting for connection';
  const reasons = { input_timeout: 'Input paused. Release the controls, then hold again.',
    telemetry_stale: 'Live angles stopped arriving. Check the connection.',
    loop_stall: 'Device paused. Release the controls, then hold again.',
    stale_command: 'Input was delayed. Release the controls, then hold again.',
    enable_refused: 'Device could not start. Release the controls and wait for live angles.' };
  const message = !state.ready ? connectionHint(state)
    : router.blocked && (mouse.held || controller.held) ? 'Release the mouse and L1, then hold your chosen control.'
    : warning || (following ? 'X or Circle stops follow. L1 or mouse cancels for manual control.'
      : intent.held ? 'Release to stop.' : state.uwb_ready ? 'Drag the mouse pad, hold L1 + stick, or press X for UWB.'
      : 'Drag the mouse pad or hold L1 + stick. UWB follow is optional.');
  ui['status-message'].textContent = reasons[message] || message;
  ui['status-message'].classList.toggle('error-text', !!warning || d?.can_ready === false);
  ui.pad.classList.toggle('live', moving);
  ui['pad-lock'].hidden = !!state.ready && !router.blocked;
  ui['pad-lock'].textContent = state.ready ? 'RELEASE CONTROLS TO START' : 'CONNECT TO MOVE';
  document.querySelectorAll('[data-x]').forEach(button => { button.disabled = !state.ready; });
  ui['active-source'].textContent = following ? 'UWB FOLLOW' : intent.source === 'mouse' ? 'MOUSE ACTIVE'
    : intent.source === 'gamepad' ? 'PS4 ACTIVE' : 'AUTO INPUT';
  ui['input-status'].textContent = following ? '● Following tag · X to stop' : intent.source === 'mouse' ? '● Mouse held' : intent.source === 'gamepad'
    ? '● L1 held · stick active' : '● Mouse and controller available';
  ui['controller-status'].textContent = controller.supported ? (state.uwb_ready
    ? 'PS4 connected · L1 + stick · X: UWB follow' : 'PS4 connected · L1 + stick')
    : padId ? 'Controller mapping unsupported · try Chrome' : 'PS4: connect to your Mac, then press a button';
  ui['controller-status'].title = padId;
  ui['input-note'].textContent = 'Drag the pad or hold L1 + left stick. Release to stop.';
  const u = d?.uwb;
  const tagFresh = available() && !!state.uwb_ready;
  const tagDetected = fresh && !!u?.observed;
  ui['uwb-badge'].textContent = following && state.enabled ? 'FOLLOWING' : tagFresh ? 'TAG LIVE'
    : tagDetected ? 'TAG DETECTED' : fresh && u ? 'NO UWB' : 'WAITING';
  ui['uwb-badge'].classList.toggle('live', tagFresh);
  ui['uwb-bearing'].textContent = tagFresh ? `${u.bearing.toFixed(1)}°` : '—';
  ui['uwb-range'].textContent = tagFresh ? `${u.range_m.toFixed(2)} m` : '—';
  ui['uwb-observed'].textContent = tagDetected
    ? `Receiving tag ${u.observed_tag.toString(16).toUpperCase().padStart(4, '0')}` : 'No recent anchor report';
  ui['uwb-status'].textContent = !state.connected ? 'Connect the RS5 controller. UWB follow is optional.'
    : !u ? 'Update the Feather with the combined manual + UWB firmware.'
    : !state.ready ? 'Waiting for the RS5 connection. UWB follow is optional.'
    : !tagDetected ? (u.transport === 'esp-now' && !u.radio_ready
      ? 'Set up the camera connection to add UWB follow. Manual control is available.'
      : 'No UWB tag detected. Power the camera anchor and tag to add follow. Manual control is available.')
    : !uwbSettingsMatch() ? 'Enter the tag ID and apply settings. Follow starts only when you press X.'
    : !tagFresh ? `Waiting for a fresh tag: ${u.reason}.`
    : `Tag ${u.tag.toString(16).toUpperCase().padStart(4, '0')} ready · pan only · ${u.max_speed}°/s limit.`;
  ui['uwb-apply'].disabled = !state.ready || !u || !!state.enabled || !!d?.armed;
  ui['uwb-follow'].textContent = following ? 'Stop UWB follow · X' : 'Start UWB follow · X';
  ui['uwb-follow'].disabled = !following && (!tagFresh || !uwbSettingsMatch() || !!state.release_required || !!state.enabled);
  ui.diagnostic.textContent = d
    ? `${d.replies} valid replies · ${d.bus_errors} bus errors · ${d.rx_missed} missed · ${d.fifo_overrun} overruns · REC ${d.rec} / TEC ${d.tec}. TX failures: ${d.tx_failed ?? '—'}; arbitration waits: ${d.arbitration_lost ?? '—'}. Device: ${d.reason}.`
    : 'Waiting for manual-control firmware. Close other serial monitors before connecting.';
  ui.events.textContent = state.events?.map(e => `${e.time}  ${e.message}`).join('\n') || 'No events yet.';
  ui.puck.style.left = `${50 + intent.x * 40}%`;
  ui.puck.style.top = `${50 + intent.y * 40}%`;
}
async function refreshPorts() {
  try {
    const response = await fetch('/api/ports');
    if (!response.ok) throw new Error('USB discovery unavailable');
    const ports = await response.json();
    ui.port.replaceChildren();
    for (const port of ports) {
      const option = document.createElement('option');
      option.value = port.device; option.textContent = port.label; ui.port.append(option);
    }
    if (!ports.length) {
      const option = document.createElement('option');
      option.value = ''; option.textContent = 'No USB device found'; ui.port.append(option);
    }
  } catch (error) { warning = error.message; }
  render();
}
ui.refresh.addEventListener('click', refreshPorts);
ui.port.addEventListener('change', render);
ui.connect.addEventListener('click', () => {
  following = followAcknowledged = false; cross.reset();
  router.reset(); clearMouse(); warning = ''; seq = 0;
  send(state.connected ? { type: 'disconnect' } : { type: 'connect', port: ui.port.value });
});
ui['uwb-follow'].addEventListener('click', toggleFollow);
ui['uwb-apply'].addEventListener('click', () => {
  if (state.enabled || state.device?.armed) return;
  const zero = Number(ui['uwb-zero'].value), maximum = Number(ui['uwb-speed'].value);
  if (!/^[0-9a-f]{4}$/i.test(ui['uwb-tag'].value) || ui['uwb-zero'].value === '' ||
      ui['uwb-speed'].value === '' || !Number.isFinite(zero) || Math.abs(zero) > 30 ||
      !Number.isFinite(maximum) || maximum < 1 || maximum > 15) {
    warning = 'Use a four-digit hex tag, center offset −30° to 30°, and follow limit 1–15°/s.';
    render(); return;
  }
  // Match the device's tenths-of-a-degree representation after acknowledgement.
  ui['uwb-zero'].value = (Math.round(zero * 10) / 10).toString();
  ui['uwb-speed'].value = (Math.round(maximum * 10) / 10).toString();
  send({ type: 'uwb_config', ticket, tag: ui['uwb-tag'].value.toUpperCase(),
    direction: Number(ui['uwb-direction'].value), zero: Number(ui['uwb-zero'].value),
    max_speed: Number(ui['uwb-speed'].value) });
  warning = '';
});
for (const id of ['uwb-tag', 'uwb-direction', 'uwb-zero', 'uwb-speed']) {
  ui[id].addEventListener('input', () => {
    if (following || state.enabled) cancel('Settings changed. Apply settings before restarting UWB follow.');
    render();
  });
}
for (const id of ['speed', 'invert-pan', 'invert-tilt']) ui[id].addEventListener('input', () => {
  ui['speed-value'].textContent = ui.speed.value;
  if (following || router.source || state.enabled) cancel('Settings changed. Release the controls, then hold again.');
});
function pointerVector(event) {
  const rect = ui.pad.getBoundingClientRect();
  mouse = { held: true, ...shapeStick((event.clientX - rect.left - rect.width / 2) / (rect.width * .4),
    (event.clientY - rect.top - rect.height / 2) / (rect.height * .4), .03, 1.25) };
}
ui.pad.addEventListener('pointerdown', event => {
  if (!available() || event.button !== 0 || pointer !== null) return;
  event.preventDefault(); pointer = event.pointerId; ui.pad.setPointerCapture(pointer);
  pointerVector(event); pump(performance.now(), true);
});
ui.pad.addEventListener('pointermove', event => {
  if (pointer !== event.pointerId) return;
  if (event.buttons === 0) { clearMouse(); pump(performance.now(), true); return; }
  pointerVector(event);
});
function release(event) {
  if (event.pointerId !== pointer) return;
  clearMouse(); pump(performance.now(), true);
}
for (const element of [ui.pad, ...document.querySelectorAll('[data-x]')]) {
  for (const event of ['pointerup', 'pointercancel', 'lostpointercapture']) element.addEventListener(event, release);
  element.addEventListener('contextmenu', event => event.preventDefault());
}
document.querySelectorAll('[data-x]').forEach(button => button.addEventListener('pointerdown', event => {
  if (!available() || event.button !== 0 || pointer !== null) return;
  event.preventDefault(); pointer = event.pointerId; button.setPointerCapture(pointer);
  mouse = { held: true, x: Number(button.dataset.x), y: Number(button.dataset.y) };
  pump(performance.now(), true);
}));
window.addEventListener('blur', () => cancel());
document.addEventListener('visibilitychange', () => { if (document.hidden) cancel(); });
window.addEventListener('pagehide', () => cancel());
window.addEventListener('keydown', event => {
  if (event.code === 'Space' || event.code === 'Escape') { event.preventDefault(); cancel('Stopped. Release the controls, then hold again.'); }
});
window.addEventListener('gamepaddisconnected', event => {
  if (event.gamepad.index === padIndex && (following || router.source === 'gamepad')) cancel('Controller disconnected.');
});
function animate(now) {
  if ((following || router.source) && previousFrame && now - previousFrame > 150) cancel('Input paused. Release the controls, then hold again.');
  previousFrame = now;
  let pads = [];
  try { pads = [...(navigator.getGamepads?.() || [])].filter(p => p?.connected); } catch { /* unsupported browser */ }
  const pad = pads.find(p => p.index === padIndex && p.id === padId)
    || pads.find(p => /054c|dualshock|wireless controller/i.test(p.id)) || pads[0];
  if ((pad?.index ?? null) !== padIndex || (pad?.id || '') !== padId) {
    cross.reset();
    if (following || router.source === 'gamepad') cancel('Controller changed. Release the controls, then start again.');
  }
  padIndex = pad?.index ?? null; padId = pad?.id || '';
  const previousCircle = controller.stop;
  const previouslySupported = controller.supported;
  controller = gamepadInput(pad);
  if (following && previouslySupported && !controller.supported) cancel('Controller mapping lost.');
  if (cross.sample(controller.follow, available() && controller.supported)) toggleFollow();
  if (controller.stop && !previousCircle) cancel('Stopped with Circle. Release L1, then hold again.');
  if (!available() && (following || router.source || mouse.held)) cancel();
  pump(now); render(); requestAnimationFrame(animate);
}
ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
ws.addEventListener('open', () => { warning = ''; refreshPorts(); });
ws.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.type === 'error') { following = followAcknowledged = false; cross.reset(); router.reset(); clearMouse(); warning = message.message; }
  if (message.type === 'state') {
    state = message; ticket = message.ticket; lastState = performance.now();
    if (following && (state.release_required || !state.ready || !state.uwb_ready ||
        (followAcknowledged && (!state.enabled || state.source !== 'uwb')))) {
      following = followAcknowledged = false; cross.reset(); router.reset(); clearMouse();
      warning = state.reason || 'UWB follow stopped. Press X again to restart.';
    }
    if (following && state.enabled && state.source === 'uwb') followAcknowledged = true;
    if ((!state.ready || state.release_required) && router.source) {
      router.reset(); clearMouse(); warning = state.reason;
    }
  }
  render();
});
ws.addEventListener('close', () => {
  following = followAcknowledged = false; cross.reset();
  router.reset(); clearMouse(); state = {}; warning = warning || 'App disconnected. Reload this page to reconnect.'; render();
});
ws.addEventListener('error', () => { warning = 'Cannot reach the local app. Start the server, then reload.'; render(); });
requestAnimationFrame(animate);
