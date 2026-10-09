"use strict";
// Projector view: QR code, live device progress and the anonymised queue.
const $ = id => document.getElementById(id);
const POLICIES = {fcfs: "先来先服务 FCFS", sjf: "短作业优先 SJF", balanced: "综合调度"};
const pad = value => String(value).padStart(2, "0");
const clock = ms => { const d = new Date(ms); return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`; };
const known = row => typeof row.estimated_finish_ms === "number";
const spec = row => `${(row.paper || "a4").toUpperCase()} ${row.color ? "彩色" : "黑白"} ${row.duplex ? "双面" : "单面"}`;
function duration(ms) {
  const s = Math.floor((Math.max(ms, 0) + 500) / 1000);
  return s < 1 ? "不到 1 秒" : s < 60 ? `约 ${s} 秒` : s < 3600 ? `约 ${Math.floor(s / 60)} 分 ${s % 60} 秒` : `约 ${Math.floor(s / 3600)} 小时 ${Math.floor(s % 3600 / 60)} 分`;
}
function node(tag, className, content) {
  const item = document.createElement(tag);
  if (className) item.className = className;
  if (content !== undefined) item.textContent = content;
  return item;
}

function render(state) {
  const now = state.paused ? state.forecast_at_ms : state.now_ms;
  const code = new URL(state.join_url).searchParams.get("code") || "";
  $("join-code").textContent = code;
  $("join-link").textContent = new URL(state.join_url).origin;
  $("join-link").href = state.join_url;
  $("network-hint").textContent = state.access_hint;
  $("connection").className = "connection " + (!state.shop_enabled ? "error" : state.paused ? "waiting" : "ready");
  $("connection").querySelector("span").textContent = !state.shop_enabled ? "需要经营模式" : state.paused ? "店铺暂停派发" : "店铺营业中";
  $("policy").textContent = `当前策略：${POLICIES[state.policy] || state.policy}${state.paused ? " · 已暂停派发，正在打印的订单继续" : " · 正在接单"}`;
  const printing = state.devices.filter(d => d.busy).length;
  const clear = typeof state.queue_clear_ms === "number" ? duration(state.queue_clear_ms - now) : state.queue_total || printing ? "暂无法预计" : "无需等待";
  $("counts").replaceChildren(...[["排队中", state.counts.queued], ["打印中", state.counts.running], ["已完成", state.counts.completed], ["预计清空队列", clear]]
    .map(([label, value]) => { const tile = node("div", "metric"); tile.append(node("span", "", label), node("strong", "", value)); return tile; }));
  $("devices").replaceChildren(...state.devices.map(device => {
    const percent = device.total ? Math.floor(device.printed * 100 / device.total) : 0;
    const row = node("article", "device" + (device.busy ? " busy" : ""));
    const head = node("header");
    head.append(node("strong", "", device.name), node("span", "pill " + (!device.online ? "waiting" : device.busy ? "info" : "ready"),
      (device.busy ? `打印中 ${percent}%` : "空闲") + (device.online ? "" : " · 暂停接新单")));
    row.append(head);
    if (device.busy) {
      const bar = node("div", "bar"), fill = node("i");
      fill.style.width = `${percent}%`;
      bar.append(fill);
      const meter = node("div", "meter");
      meter.append(bar, node("span", "", `${device.printed} / ${device.total} 面`));
      row.append(meter);
    }
    return row;
  }));
  const shown = state.queue.slice(0, 10);
  $("queue-summary").textContent = `共 ${state.queue_total} 单`;
  if (!shown.length) {
    const empty = node("li", "empty");
    empty.append(node("strong", "", "现在不用排队"), node("span", "", "扫码下单后，订单按当前策略排在这里"));
    $("queue").replaceChildren(empty);
  } else {
    $("queue").replaceChildren(...shown.map((job, index) => {
      const row = node("li");
      const body = node("div");
      body.append(node("strong", "", `${spec(job)} · ${job.sheets} 面`));
      const wait = job.estimated_start_ms - now;
      const start = !known(job) ? "暂无法预计" : !state.paused ? `${clock(job.estimated_start_ms)} 开始`
        : wait < 1000 ? "恢复后立即开始" : "恢复后再等" + duration(wait);
      row.append(node("b", "", index + 1), body, node("small", "", start));
      return row;
    }));
  }
}

async function poll() {
  try {
    const response = await fetch("/api/state", {credentials: "same-origin", cache: "no-store"});
    if (response.status === 401) { location.href = "/"; return; }
    render(await response.json());
  } catch {
    $("connection").className = "connection error";
    $("connection").querySelector("span").textContent = "店铺暂时连不上";
  }
  setTimeout(poll, 1500);
}
poll();
