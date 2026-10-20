"use strict";
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const test = require("node:test");

const source = fs.readFileSync(path.join(__dirname, "../tools/classroom/app.js"), "utf8");
const boot = source.lastIndexOf("\nwire();");
assert.ok(boot > 0, "The app startup boundary must exist");

function harness() {
  const elements = new Map();
  const get = id => {
    if (!elements.has(id)) {
      const classes = new Set();
      const attributes = {};
      elements.set(id, {
        textContent: "", hidden: false, title: "", attributes,
        setAttribute(name, value) { attributes[name] = value; },
        classList: {
          add(value) { classes.add(value); }, remove(value) { classes.delete(value); },
          contains(value) { return classes.has(value); },
          toggle(value, enabled) { if (enabled) classes.add(value); else classes.delete(value); return enabled; },
        },
      });
    }
    return elements.get(id);
  };
  const context = vm.createContext({document: {getElementById: get}, window: {}, console});
  vm.runInContext(source.slice(0, boot), context);
  return {get, run: code => vm.runInContext(code, context)};
}

test("selected files turn the duplicate summary into an add-more control", () => {
  const h = harness();
  h.run(`docs.push({pages:9, record:{name:"case-study.docx", note:"Office preview unavailable"}}); renderFilePicker()`);
  assert.equal(h.get("file-name").textContent, "已选 1 个文件");
  assert.equal(h.get("choose-text").textContent, "添加文件");
  assert.equal(h.get("choose-label").attributes["aria-label"], "继续添加文件");
  assert.equal(h.get("remove-file").hidden, true);
  assert.match(h.get("file-info").textContent, /还可添加 9 个/);

  h.run(`docs.push({pages:2, record:{name:"appendix.pdf", note:"PDF preview"}}); renderFilePicker()`);
  assert.equal(h.get("file-name").textContent, "已选 2 个文件");
  assert.match(h.get("file-info").textContent, /还可添加 8 个/);
});
