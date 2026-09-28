import { shapeStick, gamepadInput, speeds, connectionHint, InputRouter } from './input.mjs';

const $ = id => document.getElementById(id);
const ui = Object.fromEntries(['port','connect','refresh','connection-label','port-hint','demo-banner',
  'input-note','input-status','pad','puck','pad-lock','yaw','pitch','roll','live-badge','can-health',
  'speed','speed-value','invert-pan','invert-tilt','control-state','enabled-dot','status-message',
  'diagnostic','events','controller-status','active-source'].map(id => [id, $(id)]));
const router = new InputRouter();
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
  router.reset(); clearMouse();
  intent = { held: false, source: null, x: 0, y: 0 };
  send({ type: 'stop' });
  if (reason) warning = reason;
}
function available() {
  return !!state.connected && !!state.ready && performance.now() - lastState < 250 &&
    document.hasFocus() && !document.hidden;
}
function pump(now, force = false) {
  const previousSource = router.source;
  intent = router.step(mouse, controller, available());
  // Device/bridge failures require a fresh hold, never replay a held gesture.
  if (state.release_required && intent.held) {
    router.reset(); clearMouse();
    intent = { held: false, source: null, x: 0, y: 0 };
    warning = state.reason || 'Release the controls, then hold again.';
  } else if (!previousSource && intent.source) warning = '';
  if (state.connected && performance.now() - lastState < 250 && (force || now - lastSend >= 50)) {
    const output = intent.held
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
  ui['control-state'].textContent = moving ? 'Following your input' : state.ready ? 'Ready to move' : 'Waiting for connection';
  const reasons = { input_timeout: 'Input paused. Release the controls, then hold again.',
    telemetry_stale: 'Live angles stopped arriving. Check the connection.',
    loop_stall: 'Device paused. Release the controls, then hold again.',
    stale_command: 'Input was delayed. Release the controls, then hold again.',
    enable_refused: 'Device could not start. Release the controls and wait for live angles.' };
  const message = !state.ready ? connectionHint(state)
    : router.blocked && (mouse.held || controller.held) ? 'Release the mouse and L1, then hold your chosen control.'
    : warning || (intent.held ? 'Release to stop.' : 'Drag the mouse pad, or hold L1 and use the left stick.');
  ui['status-message'].textContent = reasons[message] || message;
  ui['status-message'].classList.toggle('error-text', !!warning || d?.can_ready === false);
  ui.pad.classList.toggle('live', moving);
  ui['pad-lock'].hidden = !!state.ready && !router.blocked;
  ui['pad-lock'].textContent = state.ready ? 'RELEASE CONTROLS TO START' : 'CONNECT TO MOVE';
  document.querySelectorAll('[data-x]').forEach(button => { button.disabled = !state.ready; });
  ui['active-source'].textContent = intent.source === 'mouse' ? 'MOUSE ACTIVE'
    : intent.source === 'gamepad' ? 'PS4 ACTIVE' : 'AUTO INPUT';
  ui['input-status'].textContent = intent.source === 'mouse' ? '● Mouse held' : intent.source === 'gamepad'
    ? '● L1 held · stick active' : '● Mouse and controller available';
  ui['controller-status'].textContent = controller.supported ? 'PS4 connected · hold L1 + left stick'
    : padId ? 'Controller mapping unsupported · try Chrome' : 'PS4: connect to your Mac, then press a button';
  ui['controller-status'].title = padId;
  ui['input-note'].textContent = 'Drag the pad or hold L1 + left stick. Release to stop.';
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
  router.reset(); clearMouse(); warning = ''; seq = 0;
  send(state.connected ? { type: 'disconnect' } : { type: 'connect', port: ui.port.value });
});
for (const id of ['speed', 'invert-pan', 'invert-tilt']) ui[id].addEventListener('input', () => {
  ui['speed-value'].textContent = ui.speed.value;
  if (router.source || state.enabled) cancel('Settings changed. Release the controls, then hold again.');
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
  if (event.gamepad.index === padIndex && router.source === 'gamepad') cancel('Controller disconnected.');
});
function animate(now) {
  if (router.source && previousFrame && now - previousFrame > 150) cancel('Input paused. Release the controls, then hold again.');
  previousFrame = now;
  let pads = [];
  try { pads = [...(navigator.getGamepads?.() || [])].filter(p => p?.connected); } catch { /* unsupported browser */ }
  const pad = pads.find(p => p.index === padIndex && p.id === padId)
    || pads.find(p => /054c|dualshock|wireless controller/i.test(p.id)) || pads[0];
  if (router.source === 'gamepad' && (pad?.index !== padIndex || pad?.id !== padId)) cancel('Controller changed. Release L1, then hold again.');
  padIndex = pad?.index ?? null; padId = pad?.id || '';
  const previousCircle = controller.stop;
  controller = gamepadInput(pad);
  if (controller.stop && !previousCircle) cancel('Stopped with Circle. Release L1, then hold again.');
  if (!available() && (router.source || mouse.held)) cancel();
  pump(now); render(); requestAnimationFrame(animate);
}
ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
ws.addEventListener('open', () => { warning = ''; refreshPorts(); });
ws.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.type === 'error') { router.reset(); clearMouse(); warning = message.message; }
  if (message.type === 'state') {
    state = message; ticket = message.ticket; lastState = performance.now();
    if ((!state.ready || state.release_required) && router.source) {
      router.reset(); clearMouse(); warning = state.reason;
    }
  }
  render();
});
ws.addEventListener('close', () => {
  router.reset(); clearMouse(); state = {}; warning = warning || 'App disconnected. Reload this page to reconnect.'; render();
});
ws.addEventListener('error', () => { warning = 'Cannot reach the local app. Start the server, then reload.'; render(); });
requestAnimationFrame(animate);
