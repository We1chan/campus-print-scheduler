"use strict";
// Exercise the actual classic-script controller with deterministic DOM/animation
// boundaries. Real image rendering and geometry still require browser checks.
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const test = require("node:test");

const source = fs.readFileSync(path.join(__dirname, "../tools/classroom/app.js"), "utf8");
const boot = source.lastIndexOf("\nwire();");
assert.ok(boot > 0, "The app's startup boundary must exist");

function harness() {
  const elements = new Map();
  const get = id => {
    if (!elements.has(id)) elements.set(id, {
      dataset: {}, hidden: false, textContent: "", firstChild: {}, clientWidth: 400, clientHeight: 600,
      style: {setProperty() {}}, setAttribute() {}, getBoundingClientRect() { return {}; },
      classList: {contains: () => false, add() {}, remove() {}, toggle() {}},
      animate() {
        return {reversals: 0, cancel() {}, reverse() { this.reversals += 1; }, finish() { this.onfinish(); }};
      },
    });
    return elements.get(id);
  };
  const context = vm.createContext({document: {getElementById: get}, window: {devicePixelRatio: 1}, console,
    getComputedStyle: () => ({getPropertyValue: key => key === "--book-duration" ? "280ms" : "cubic-bezier(.77, 0, .175, 1)"}),
    matchMedia: () => ({matches: false}), requestAnimationFrame: callback => callback(), clearTimeout() {},
  });
  vm.runInContext(source.slice(0, boot), context);
  const run = code => vm.runInContext(code, context);
  run(`let testPages=8, testDuplex=true, testReduced=false, lastPaint=null, filled={};
    const realLayout=layoutSheet;
    options=()=>({duplex:testDuplex,color:true}); printedPages=()=>testPages;
    reducedBookMotion=()=>testReduced; warmBookPages=()=>{};
    prepareBookPaper=page=>({page,content:page===null?null:{querySelectorAll:()=>[]}});
    fillPaper=(id,page,content)=>{filled[id]=content}; paintBook=prepared=>{lastPaint=prepared};
    layoutSheet=()=>{}; updateSummary=()=>{};
  `);
  return {run, get, value: code => JSON.parse(JSON.stringify(run(code))), finish: () => run("bookMotion.animation.finish()")};
}
const flush = () => new Promise(resolve => setImmediate(resolve));

test("cover, consecutive spreads, back cover and both bounds", () => {
  const h = harness();
  assert.deepEqual(h.value("spreadAt(0)"), {left: null, right: 1, state: "cover"});
  h.run("step(1); step(1); step(1); step(1)");
  for (let turn = 1; turn <= 4; turn += 1) { h.finish(); assert.equal(h.run("bookTurn"), turn); }
  assert.deepEqual(h.value("spreadAt(bookTurn)"), {left: 8, right: null, state: "back-cover"});
  h.run("step(1)"); assert.equal(h.run("bookMotion"), null);
  h.run("step(-4)"); for (let i = 0; i < 4; i += 1) h.finish();
  h.run("step(-1)"); assert.equal(h.run("bookTurn"), 0); assert.equal(h.run("bookMotion"), null);
});

test("reverse twice and reuse already decoded physical faces at either endpoint", () => {
  const h = harness();
  h.run("step(1); step(-1); step(1)");
  assert.equal(h.run("bookMotion.animation.reversals"), 2);
  const back = h.run('filled["face-back"]'), right = h.run('filled["book-right"]');
  h.finish(); assert.equal(h.run("bookTurn"), 1);
  assert.equal(h.run("lastPaint.left.content"), back); assert.equal(h.run("lastPaint.right.content"), right);
  h.run("step(-1); step(1)");
  const sameBack = h.run('filled["face-back"]'); h.finish();
  assert.equal(h.run("bookTurn"), 1); assert.equal(h.run("lastPaint.left.content"), sameBack);
  h.run("step(-1)"); const front = h.run('filled["face-front"]'); h.finish();
  assert.equal(h.run("bookTurn"), 0); assert.equal(h.run("lastPaint.right.content"), front);
});

test("single sided, odd and one-page documents retain unprinted backs", () => {
  const h = harness(); h.run("testPages=7");
  assert.deepEqual(h.value("spreadAt(4)"), {left: 0, right: null, state: "back-cover"});
  h.run("testDuplex=false"); assert.deepEqual(h.value("spreadAt(2)"), {left: 0, right: 3, state: "open"});
  h.run("testPages=1"); assert.deepEqual(h.value("spreadAt(1)"), {left: 0, right: null, state: "back-cover"});
});

test("reset, reduced motion and keyboard discard active animation callbacks", () => {
  const h = harness(); h.run("step(1); const stale=bookMotion.animation; resetBook(); stale.finish()");
  assert.equal(h.run("bookTurn"), 0);
  h.run("step(1); testReduced=true; step(1)");
  assert.equal(h.run("bookTurn"), 2); assert.equal(h.run("bookMotion"), null);
  h.run("testReduced=false; step(-1,true)");
  assert.equal(h.run("bookTurn"), 1); assert.equal(h.run("bookMotion"), null);
});

function delayImages(h) {
  h.run(`let resolveImages; const delayedImage={decode:()=>new Promise(resolve=>{resolveImages=resolve})};
    prepareBookPaper=page=>({page,content:{querySelectorAll:()=>page===2?[delayedImage]:[]}});`);
}
test("cold images keep the centered cover until all faces are ready", async () => {
  const h = harness(); delayImages(h); h.run("layoutSheet=realLayout; step(1)");
  assert.equal(h.run("bookTurn"), 0); assert.equal(h.run("bookMotion.animation"), null);
  h.run("layoutSheet()"); assert.equal(h.get("book").dataset.state, "cover");
  assert.equal(h.get("book").dataset.loading, "true");
  h.run("resolveImages()"); await flush();
  assert.equal(h.run("bookMotion.started"), true); assert.equal(h.get("book").dataset.state, "open");
  h.finish(); assert.equal(h.run("bookTurn"), 1);
});
test("cancel while decoding ignores the late result", async () => {
  const h = harness(); delayImages(h); h.run("step(1); step(-1)");
  assert.equal(h.run("bookMotion"), null);
  h.run("resolveImages()"); await flush();
  assert.equal(h.run("bookTurn"), 0); assert.equal(h.run("bookMotion"), null);
});
test("failed image decoding does not lock navigation", async () => {
  const h = harness();
  h.run(`prepareBookPaper=page=>({page,content:{querySelectorAll:()=>page===2?[{decode:()=>Promise.reject(new Error('render failed'))}]:[]}}); step(1)`);
  await flush(); h.finish(); assert.equal(h.run("bookTurn"), 1);
});
test("first import and adding a document reset an open preview to its cover", async () => {
  const h = harness();
  h.run(`persistDocs=()=>{}; renderFileList=()=>{}; recalcPages=()=>8; updateActions=()=>{};
    syncPages=()=>{}; scheduleQuote=()=>{}; pollConversion=()=>{}; say=()=>{};
    uploadOne=async()=>({upload_id:'a'.repeat(32),pages:8,pages_locked:true,preview:'pdf',status:'ready'});`);
  for (let count = 1; count <= 2; count += 1) {
    h.run("bookTurn=bookTarget=2; rotation=90; aspects.set(1,0.7)");
    await h.run("chooseFiles([{name:'book.pdf',size:100}])");
    assert.equal(h.run("docs.length"), count);
    assert.equal(h.run("bookTurn"), 0); assert.equal(h.run("bookTarget"), 0);
    assert.equal(h.run("rotation"), 0); assert.equal(h.run("aspects.size"), 0);
  }
});
