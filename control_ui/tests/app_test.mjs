// Execute the real frontend with a small event/DOM fixture, without browser or hardware.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import * as input from '../static/input.mjs';
const html = readFileSync(new URL('../static/index.html', import.meta.url), 'utf8');
assert(!/id="(?:enable|stop|mouse-tab|gamepad-tab)"/.test(html));
class Target {
  listeners = {};
  addEventListener(name, fn) { (this.listeners[name] ||= []).push(fn); }
  emit(name, event = {}) { for (const fn of this.listeners[name] || []) fn(event); }
}
class Element extends Target {
  value = ''; checked = false; style = {}; dataset = {}; children = [];
  classList = {toggle() {}};
  captures = new Set();
  setPointerCapture(id) { this.captures.add(id); }
  hasPointerCapture(id) { return this.captures.has(id); }
  releasePointerCapture(id) { this.captures.delete(id); }
  getBoundingClientRect() { return {left:0,top:0,width:200,height:200}; }
  replaceChildren() { this.children = []; }
  append(child) { this.children.push(child); }
  querySelector() { return new Element(); }
}
const ids = Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m => [m[1],new Element()]));
ids.speed.value = '10';
const jog = [...html.matchAll(/data-x="([^"]+)" data-y="([^"]+)"/g)].map(m => {
  const el = new Element(); el.dataset = {x:m[1],y:m[2]}; return el;
});
let focused = true, now = 1000, frame;
let pads = [];
const document = new Target();
Object.assign(document, {hidden:false, getElementById:id=>ids[id], hasFocus:()=>focused,
  querySelectorAll:()=>jog, createElement:()=>new Element()});
const window = new Target();
let socket;
class Socket extends Target {
  static OPEN = 1;
  readyState = 1; bufferedAmount = 0; sent = [];
  constructor() { super(); socket = this; }
  send(line) { this.sent.push(JSON.parse(line)); }
  close() { this.readyState = 3; this.emit('close'); }
}
const context = vm.createContext({...input, document, window, WebSocket:Socket,
  location:{protocol:'http:',host:'127.0.0.1:8765'}, performance:{now:()=>now},
  navigator:{getGamepads:()=>pads}, requestAnimationFrame:fn=>{frame=fn;},
  fetch:async()=>({ok:true,json:async()=>[]}), console});
const app = readFileSync(new URL('../static/app.js',import.meta.url),'utf8').replace(/^import .*;\n/, '');
vm.runInContext(app, context);
function state(overrides={}) {
  socket.emit('message',{data:JSON.stringify({type:'state',connected:true,ready:true,enabled:false,
    release_required:false,telemetry_age_ms:0,ticket:'test',events:[],
    device:{fresh:true,can_ready:true,probing:true,yaw:0,pitch:0,roll:0,bus_errors:0,replies:5},...overrides})});
}
function tick() { now+=50; state(); frame(now); }
function latestInput() { return socket.sent.filter(m=>m.type==='input').at(-1); }
function pointer(pointerId=1) { return {pointerId,button:0,buttons:1,clientX:180,clientY:100,preventDefault(){}}; }
tick(); // Observe released inputs.
ids.pad.emit('pointerdown',pointer());
assert.equal(latestInput().held,true);
assert.equal(latestInput().source,'mouse');
assert.equal(latestInput().pan,10);
ids.pad.emit('pointerup',pointer());
assert.equal(latestInput().held,false);
assert.equal(latestInput().pan,0);
const pad = {id:'DualShock',index:0,connected:true,mapping:'standard',axes:[.5,0],
  buttons:Array.from({length:17},()=>({pressed:false}))};
pads=[pad]; tick();
assert.equal(latestInput().held,false); // A stick alone never controls the gimbal.
pad.buttons[4].pressed=true; tick();
assert.equal(latestInput().source,'gamepad');
assert(latestInput().pan>0 && latestInput().pan<10);
pad.buttons[4].pressed=false; tick();
assert.equal(latestInput().held,false);
pad.buttons[4].pressed=true; tick();
assert.equal(latestInput().held,true);
focused=false; window.emit('blur'); tick();
assert.equal(latestInput().held,false);
focused=true; tick();
assert.equal(latestInput().held,false); // Returning focus doesn't replay L1.
pad.buttons[4].pressed=false; tick();
pad.buttons[4].pressed=true; tick();
assert.equal(latestInput().held,true);
state({ready:false, release_required:true});
now += 50; frame(now);
assert.equal(latestInput().held,false);
tick();
assert.equal(latestInput().held,false); // Recovery also requires releasing L1.
pad.buttons[4].pressed=false; tick();
pad.buttons[4].pressed=true; tick();
assert.equal(latestInput().held,true);
assert(!socket.sent.some(m=>m.type==='arm'||m.type==='drive')); // UI uses held-input protocol only.
console.log('Real frontend event flow: no enable/mode/stop buttons; mouse/L1 start, release, focus loss and fault recovery passed.');
