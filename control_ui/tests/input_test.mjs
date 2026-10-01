import assert from 'node:assert/strict';
import { shapeStick, gamepadInput, speeds, connectionHint, InputRouter } from '../static/input.mjs';
assert.match(connectionHint({connected:true, device:{can_ready:false}}), /CAN connection stopped/);
assert.match(connectionHint({connected:true}), /firmware to respond/);
assert.match(connectionHint({connected:true, device:{can_ready:true, probing:true}, telemetry_age_ms:0}), /gimbal is powered on/);
assert.match(connectionHint({connected:true, device:{can_ready:true}, telemetry_age_ms:500}), /USB/);
assert.match(connectionHint({connected:true, ready:true, device:{can_ready:true, probing:true}, telemetry_age_ms:10}), /Live angles received/);
assert.deepEqual(shapeStick(.05, -.05), {x:0, y:0});
assert.deepEqual(shapeStick(NaN, 0), {x:0, y:0});
assert.deepEqual(shapeStick(1, 0), {x:1, y:0});
const partial = shapeStick(.5, 0);
assert(partial.x > .2 && partial.x < .4); // Fine control at partial deflection.
assert(Math.hypot(...Object.values(shapeStick(1, 1))) <= 1.00000001);
assert.deepEqual(speeds(shapeStick(1, 0), 100, false, false), {pan:60, tilt:-0});
assert.deepEqual(speeds(shapeStick(0, -1), 60, false, false), {pan:0, tilt:60});
assert.deepEqual(speeds(shapeStick(-1, 0), 60, false, false), {pan:-60, tilt:-0});
assert.deepEqual(speeds(shapeStick(0, 1), 60, false, false), {pan:0, tilt:-60});
assert(speeds(partial, 60, false, false).pan > 10 && speeds(partial, 60, false, false).pan < 30);
assert.deepEqual(speeds({x:1,y:-1}, 2, true, true), {pan:-2, tilt:-2});
const pad = {connected:true, mapping:'standard', axes:[.5, -.5],
  buttons:Array.from({length:17},()=>({pressed:false,value:0}))};
assert.equal(gamepadInput(pad).x, 0); // L1 is required.
pad.buttons[4].pressed = true;
assert(gamepadInput(pad).x > 0 && gamepadInput(pad).y < 0);
assert.equal(gamepadInput(pad).neutral, false);
pad.buttons[4].pressed = false; pad.axes = [0, 0];
assert.equal(gamepadInput(pad).neutral, true);
pad.buttons[1].pressed = true;
assert.equal(gamepadInput(pad).stop, true);
pad.mapping = '';
assert.equal(gamepadInput(pad).supported, false);
assert.equal(gamepadInput(null).supported, false);
console.log('Analog shaping, dead zone, speed cap, axis reversal and L1/Circle mapping passed.');
const router = new InputRouter();
const mouseUp = {held:false,x:0,y:0}, mouseDown = {held:true,x:.5,y:0};
const psUp = {supported:true,held:false,stop:false,x:0,y:0};
const psDown = {...psUp,held:true,x:0,y:-.4};
assert(!router.step(mouseUp, psDown, true).held); // Held at startup cannot start.
router.step(mouseUp, psUp, true);
assert.equal(router.step(mouseDown, psUp, true).source, 'mouse');
assert.equal(router.step(mouseDown, psDown, true).source, 'mouse'); // First source wins.
assert(!router.step(mouseUp, psDown, true).held); // No jump to a held second source.
assert(!router.step(mouseUp, psDown, true).held);
router.step(mouseUp, psUp, true);
assert.equal(router.step(mouseUp, psDown, true).source, 'gamepad');
assert(!router.step(mouseUp, psDown, false).held); // Focus/connection loss.
assert(!router.step(mouseUp, psDown, true).held); // Recovery can't replay held input.
router.step(mouseUp, psUp, true);
assert.equal(router.step(mouseUp, psDown, true).source, 'gamepad');
assert(!router.step(mouseUp, {...psDown, supported:false}, true).held); // Unplug.
assert(!router.step(mouseUp, psDown, true).held); // Must release reconnected pad.
router.step(mouseUp, psUp, true);
assert.equal(router.step(mouseUp, psDown, true).source, 'gamepad');
assert(!router.step(mouseUp, {...psDown, stop:true}, true).held);
assert(!router.step(mouseUp, psDown, true).held);
router.step(mouseUp, psUp, true);
assert.equal(router.step(mouseDown, psUp, true).source, 'mouse');
assert(!router.step(mouseUp, psUp, true).held);
console.log('Automatic device selection, first-hold ownership, release-to-stop and no held-input restart passed.');

const { PressEdge } = await import('../static/input.mjs');
const cross = new PressEdge();
assert(!cross.sample(true, true)); // Held on attach is not a press.
assert(!cross.sample(false, true));
assert(cross.sample(true, true));
assert(!cross.sample(true, true));
assert(!cross.sample(false, false)); // Release while unavailable doesn't prime.
assert(!cross.sample(true, true));
cross.sample(false, true);
assert(cross.sample(true, true));
cross.reset(); assert(!cross.sample(true, true));
pad.mapping='standard'; pad.buttons[0].pressed=true;
assert(gamepadInput(pad).follow);
console.log('X standard mapping and release-before-toggle edge detection passed.');
