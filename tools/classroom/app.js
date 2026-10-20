"use strict";
// 纸间 web student client. It only holds gateway snapshots and sends commands;
// prices, deadlines and forecasts always come from the coordinator.
const $ = id => document.getElementById(id);
const MAX_FILE = 10 * 1024 * 1024;
const MAX_DOCS = 10;
const EXTENSIONS = [".pdf", ".txt", ".md", ".png", ".jpg", ".jpeg", ".doc", ".docx", ".ppt", ".pptx", ".xls", ".xlsx"];
const PREVIEW_WIDTHS = [480, 960, 1440];
const PORTRAIT = Math.SQRT2;
const POLICIES = {fcfs: "先来先服务", sjf: "短作业优先", balanced: "综合调度"};
const STATES = {queued: "等待中", running: "打印中", completed: "已完成", failed: "失败", cancelling: "取消中", cancelled: "已取消"};
const TONES = {running: "info", completed: "ready", failed: "error", cancelling: "waiting"};
const PAYMENTS = {counter: "柜台", wechat: "微信", alipay: "支付宝", campus_card: "校园卡"};
const DISCOUNTS = {express: "原价", standard: "95 折", flexible: "85 折"};
const SAMPLE_HEADINGS = ["2.1 进程调度的基本概念", "2.2 先来先服务与短作业优先", "2.3 综合调度与等待老化", "2.4 实验结果与分析",
  "3.1 同步与互斥", "3.2 死锁的预防", "4.1 内存分页", "附录 · 实验代码"];

const storage = {
  get(area, key) { try { return window[area].getItem(key); } catch { return null; } },
  set(area, key, value) { try { value === null ? window[area].removeItem(key) : window[area].setItem(key, value); } catch { /* private mode */ } },
};

// Wording shared with the Qt clients (src/gui/view_text.hpp).
const pad = value => String(value).padStart(2, "0");
const text = {
  money: cents => `¥${(cents / 100).toFixed(2)}`,
  clock: ms => { const d = new Date(ms); return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`; },
  date: ms => { const d = new Date(ms); return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`; },
  duration(ms) {
    const s = Math.floor((Math.max(ms, 0) + 500) / 1000);
    if (s < 1) return "不到 1 秒";
    if (s < 60) return `约 ${s} 秒`;
    if (s < 3600) return `约 ${Math.floor(s / 60)} 分 ${s % 60} 秒`;
    return `约 ${Math.floor(s / 3600)} 小时 ${Math.floor(s % 3600 / 60)} 分`;
  },
  startsIn: wait => wait < 1000 ? "即将开始" : text.duration(wait) + "后开始",
  afterResume: wait => wait < 1000 ? "恢复后立即开始" : "恢复后再等" + text.duration(wait),
  blocked: reason => reason === "no_paper" ? "纸张不足，等待补货" : reason === "no_device" ? "兼容设备暂停接单" : "排队较长，暂无法预计",
  spec: row => `${(row.paper || "a4").toUpperCase()} ${row.color ? "彩色" : "黑白"} ${row.duplex ? "双面" : "单面"}`,
  capability: d => `${d.a3 ? "A3/A4" : "A4"}${d.color ? " 彩色/黑白" : " 黑白"}${d.duplex ? " 双面" : " 单面"}`,
  forecast(job, now, paused) {
    if (job.state !== "queued" && job.state !== "running") return "";
    if (!known(job)) return job.state === "queued" && job.estimate_blocked ? text.blocked(job.estimate_blocked) : "";
    if (job.state === "running") return `预计 ${text.clock(job.estimated_finish_ms)} 完成`;
    if (paused) return "派发已暂停 · " + text.afterResume(job.estimated_start_ms - now);
    return `预计 ${text.clock(job.estimated_finish_ms)} 完成 · 前面 ${job.jobs_ahead} 单`;
  },
  quoteForecast(quote) {
    if (!known(quote)) return quote.estimate_blocked ? text.blocked(quote.estimate_blocked) : "暂无法预计等待时间";
    const wait = quote.estimated_wait_ms, total = quote.estimated_finish_ms - quote.now_ms;
    if (quote.paused) return `店铺暂停派发 · ${text.afterResume(wait)}，${text.duration(total)}完成`;
    return `前面 ${quote.jobs_ahead} 单 · ${wait < 1000 ? "可立即开始" : text.duration(wait) + "后开始"} · 预计 ${text.clock(quote.estimated_finish_ms)} 完成`;
  },
};
const known = row => typeof row.estimated_finish_ms === "number";
const jobIdFor = upload => upload.replace(/^(.{8})(.{4})(.{4})(.{4})(.{12})$/, "$1-$2-$3-$4-$5");
function node(tag, className, content) {
  const item = document.createElement(tag);
  if (className) item.className = className;
  if (content !== undefined) item.textContent = content;
  return item;
}
const radio = name => document.querySelector(`input[name="${name}"]:checked`).value;
const integer = (id, low, high) => Math.min(high, Math.max(low, Math.round(Number($(id).value)) || low));

let csrf = "", view = null, connected = false;
let docs = [], uploading = null, uploadToken = 0, uploadCount = 0, conversionTimer = 0;
const activeRequests = new Set();
// One merged order may bundle several uploaded files; docs[0] anchors the job id,
// the paper preview and the reader, exactly like the old single-document flow.
const primary = () => docs.length ? docs[0] : null;
const primaryRecord = () => primary() ? primary().record : null;
const hasDocs = () => docs.length > 0;
const recordReady = record => record && record.preview === "pdf" && record.status === "ready";
const IMAGE_TYPES = [".png", ".jpg", ".jpeg"];
const TEXT_TYPES = [".txt", ".md"];
const extensionOf = name => name.includes(".") ? name.slice(name.lastIndexOf(".")).toLowerCase() : "";
const docPages = doc => doc.record.pages_locked ? doc.record.pages : (docs.length === 1 ? integer("pages", 1, 1000) : doc.pages);
const mergedDocuments = () => docs.map(doc => ({upload_id: doc.record.upload_id, pages: docPages(doc)}));
// Single file keeps the original upload_id request shape; a merge sends documents[]
// so the gateway sums the pages and anchors the job on the first file.
function orderBody(base) {
  if (!docs.length || !docs[0].record) return base;
  if (docs.length === 1) return {...base, upload_id: docs[0].record.upload_id};
  return {...base, documents: mergedDocuments()};
}
function recalcPages() {
  const single = docs.length === 1 && docs[0].record && !docs[0].record.pages_locked;
  const total = single ? integer("pages", 1, 1000) : Math.min(1000, docs.reduce((sum, doc) => sum + (doc.record ? doc.pages : 0), 0));
  if (!single) $("pages").value = total;
  if (docs.length === 1 && docs[0].record) docs[0].pages = total;
  return total;
}
function syncPages() { lockPages(!(docs.length === 1 && docs[0].record && !docs[0].record.pages_locked)); recalcPages(); }
function locate(page) {
  let offset = 0;
  for (const doc of docs) {
    const count = doc.record ? docPages(doc) : 0;
    if (page <= offset + count) return {doc, local: page - offset};
    offset += count;
  }
  return {doc: docs.length ? docs[docs.length - 1] : null, local: 1};
}
let quote = null, quoteState = "pending", quoteError = "", quoteSequence = 0, quoteTimer = 0, quoteRefresh = 0, queueSignature = "";
let bookTurn = 0, bookTarget = 0, bookMotion = null, rotation = 0, highlight = "", cancelTarget = null, readerPage = 1;
const aspects = new Map();

// ---------- connection and state ----------
class RequestError extends Error {
  constructor(message, status) { super(message); this.status = status; }
}
async function api(path, body) {
  const options = {credentials: "same-origin", cache: "no-store"};
  if (body !== undefined) {
    options.method = "POST";
    options.headers = {"X-CSRF-Token": csrf, "Content-Type": "application/json"};
    options.body = JSON.stringify(body);
  }
  let response, result;
  try { response = await fetch(path, options); } catch { throw new RequestError("连接中断，请查看我的订单核对结果；系统没有自动重发操作。", 0); }
  try { result = await response.json(); } catch { throw new RequestError("入口暂不可用，请联系展示者检查网络和隧道。", response.status); }
  if (!response.ok) throw new RequestError(result.error || "请求失败，请稍后核对订单。", response.status);
  return result;
}

let pollTimer = 0, failures = 0;
async function poll() {
  clearTimeout(pollTimer);
  try {
    render(await api("/api/state"));
    failures = 0;
  } catch (error) {
    if (error.status === 401) { location.href = "/"; return; }
    failures += 1;
    connected = false;
    renderConnection(error.message);
    updateActions();
  }
  if (!document.hidden) pollTimer = setTimeout(poll, failures ? Math.min(8000, 1500 * 2 ** failures) : 1500);
}

function render(state) {
  const first = !view;
  view = state;
  csrf = state.csrf;
  connected = true;
  renderConnection();
  $("student").placeholder = state.visitor;
  const anchor = primaryRecord();
  if (anchor && state.jobs.some(job => job.job_id === jobIdFor(anchor.upload_id))) clearDocument(true);
  renderMetrics();
  renderOrders();
  renderDevices();
  renderQueue();
  const signature = [state.queue_total, state.paused, state.policy, state.counts.running,
    state.devices.map(d => +d.online).join("")].join("|");
  if (first || signature !== queueSignature) scheduleQuote();
  queueSignature = signature;
  updateActions();
}

function renderConnection(problem) {
  const shop = view && view.shop_enabled;
  const [tone, label] = !connected ? ["error", "店铺暂时连不上"] : !shop ? ["error", "需要经营模式"]
    : view.paused ? ["waiting", "店铺暂停派发"] : ["ready", "店铺营业中"];
  $("connection").className = "connection " + tone;
  $("connection").querySelector("span").textContent = label;
  $("connection").title = problem || "";
  if (problem && !connected) say(problem, "error");
}

function say(message, tone = "") {
  $("feedback").textContent = message;
  $("feedback").className = tone;
}
let toastTimer = 0;
function toast(message) {
  const box = $("toast");
  box.textContent = message;
  box.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { box.hidden = true; }, 4200);
}

// ---------- pages ----------
function showPage() {
  const page = {"#mine": "mine", "#queue": "queue"}[location.hash] || "order";
  document.body.dataset.page = page;
  for (const section of document.querySelectorAll("main > .page")) section.hidden = section.dataset.page !== page;
  for (const tab of document.querySelectorAll(".tabs a")) {
    if (tab.dataset.page === page) tab.setAttribute("aria-current", "page"); else tab.removeAttribute("aria-current");
  }
  window.scrollTo(0, 0);
  if (page === "order") requestAnimationFrame(layoutSheet);
}

function forecastNow() { return view.paused ? view.forecast_at_ms : view.now_ms; }

function renderMetrics() {
  const mine = view.jobs, printing = view.devices.filter(d => d.busy).length;
  const live = mine.filter(job => ["queued", "running", "cancelling"].includes(job.state));
  const ready = mine.filter(job => job.state === "completed").length;
  const soonest = live.filter(known).sort((a, b) => a.estimated_finish_ms - b.estimated_finish_ms)[0];
  const clear = typeof view.queue_clear_ms === "number" ? text.duration(view.queue_clear_ms - forecastNow())
    : view.queue_total === 0 && printing === 0 ? "无需等待" : "暂无法预计";
  const tiles = [
    ["当前排队", `${view.queue_total} 单`, `${printing} / ${view.devices.length} 台设备正在打印`],
    ["预计清空队列", clear, view.paused ? "店铺暂停派发，恢复后开始计时" : "按当前队列推算，不含新订单"],
    ["我的进行中订单", `${live.length} 单`, !soonest ? "暂无可预计的进行中订单" : view.paused ? "店铺暂停派发，恢复后继续"
      : `最近一单预计 ${text.clock(soonest.estimated_finish_ms)} 完成`],
    ["可取件", `${ready} 单`, "凭“我的订单”里的取件码到店取件", ready ? "accent" : ""],
  ];
  for (const box of document.querySelectorAll("[data-metrics]")) {
    box.replaceChildren(...tiles.map(([title, value, hint, tone]) => {
      const tile = node("div", "metric");
      tile.append(node("span", "", title), node("strong", tone || "", value), node("small", "", hint));
      return tile;
    }));
  }
  const badge = $("mine-badge");
  badge.hidden = !ready;
  badge.textContent = ready;
}

function orderProgress(job) {
  let progress = text.forecast(job, forecastNow(), view.paused);
  if (job.recovering) progress = "后台正在核对旧执行结果";
  else if (job.state === "queued" && known(job) && !view.paused)
    progress = `第 ${job.jobs_ahead + 1} 位 · ${text.startsIn(job.estimated_start_ms - view.now_ms)} · 预计 ${text.clock(job.estimated_finish_ms)} 完成`;
  else if (job.state === "running" && known(job)) progress += `（还需${text.duration(job.estimated_finish_ms - view.now_ms)}）`;
  else if (job.state === "completed") progress = "已完成，可凭取件码取件";
  else if (job.state === "cancelling") progress = "正在停止打印";
  else if (job.state === "failed") progress = "打印失败";
  else if (job.state === "cancelled") progress = "已取消";
  else if (!progress) progress = "暂无法预计";
  return progress;
}

function renderOrders() {
  const jobs = view.jobs, name = $("student").value.trim() || view.visitor;
  $("orders-summary").textContent = `${name} · 共 ${jobs.length} 单`;
  if (!jobs.length) {
    const empty = node("div", "empty");
    empty.append(node("strong", "", "还没有订单"), node("span", "", "在“下单打印”选择文件并支付后，订单会出现在这里"));
    $("orders").replaceChildren(empty);
    return;
  }
  $("orders").replaceChildren(...jobs.map(job => {
    const card = node("article", "order" + (job.job_id === highlight ? " fresh" : ""));
    const head = node("header");
    head.append(node("strong", "order-title", job.title), node("span", "pill " + (TONES[job.state] || "muted"), STATES[job.state] || job.state));
    const refunded = job.state === "cancelled" || job.state === "failed";
    const paid = refunded ? "已原路退款" : job.refund_cents > 0 ? `已退款 ${text.money(job.refund_cents)}` : `${PAYMENTS[job.payment] || ""}已支付`;
    card.append(head, node("p", "order-spec", `${text.spec(job)} · ${job.sheets} 面 · ${text.money(job.quoted_cents)} · ${paid}`),
      node("p", "order-progress", orderProgress(job)));
    const bar = node("div", "bar");
    const fill = node("i");
    fill.style.width = `${job.sheets ? Math.min(100, 100 * (job.printed_sheets || 0) / job.sheets) : 0}%`;
    bar.append(fill);
    const meter = node("div", "meter");
    meter.append(bar, node("span", "", `${job.printed_sheets || 0} / ${job.sheets} 面`));
    card.append(meter);
    const foot = node("footer");
    if (job.state === "completed") {
      const code = node("p", "pickup");
      code.append(node("span", "", "取件码"), node("strong", "", job.pickup_code));
      foot.append(code);
    } else if (job.deadline_ms && ["queued", "running"].includes(job.state)) {
      foot.append(node("p", "muted", `承诺 ${text.date(job.deadline_ms)} 前可取件`));
    }
    if (["queued", "running"].includes(job.state)) {
      const cancel = node("button", "secondary small", "取消订单并退款");
      cancel.type = "button";
      cancel.disabled = !connected;
      cancel.addEventListener("click", () => askCancel(job));
      foot.append(cancel);
    }
    if (foot.childElementCount) card.append(foot);
    return card;
  }));
  // Cards are rebuilt on every poll; flash the new order only once.
  if (jobs.some(job => job.job_id === highlight)) highlight = "";
}

function renderDevices() {
  $("policy").textContent = `当前策略：${POLICIES[view.policy] || view.policy}${view.paused ? " · 店铺暂停派发，正在打印的订单继续" : " · 正在接单"}`;
  $("devices").replaceChildren(...view.devices.map(device => {
    const row = node("article", "device" + (device.busy ? " busy" : ""));
    const percent = device.total ? Math.floor(device.printed * 100 / device.total) : 0;
    const status = (device.recovering ? "恢复核对中" : device.state === "cancelling" ? "取消中"
      : device.busy ? `打印中 ${percent}%${device.mine ? " · 我的订单" : ""}` : "空闲") + (device.online ? "" : " · 暂停接新单");
    const head = node("header");
    head.append(node("strong", "", device.name), node("span", "pill " + (!device.online ? "waiting" : device.busy ? "info" : "ready"), status));
    row.append(head, node("p", "muted", text.capability(device)));
    if (device.busy) {
      const bar = node("div", "bar");
      const fill = node("i");
      fill.style.width = `${percent}%`;
      bar.append(fill);
      const meter = node("div", "meter");
      meter.append(bar, node("span", "", known(device) ? `预计 ${text.clock(device.estimated_finish_ms)} 空闲` : `${device.printed} / ${device.total} 面`));
      row.append(meter);
    }
    return row;
  }));
}

function renderQueue() {
  $("queue-summary").textContent = view.queue_total > view.queue.length ? `显示前 ${view.queue.length} 单 · 共 ${view.queue_total} 单` : `共 ${view.queue_total} 单`;
  if (!view.queue.length) {
    const empty = node("li", "empty");
    empty.append(node("strong", "", "现在不用排队"), node("span", "", "有订单等待时，这里按预计开始的先后列出"));
    $("queue").replaceChildren(empty);
    return;
  }
  $("queue").replaceChildren(...view.queue.map((job, index) => {
    const row = node("li", job.own ? "own" : "");
    const start = !known(job) ? text.blocked(job.estimate_blocked) : view.paused ? text.afterResume(job.estimated_start_ms - forecastNow())
      : `${text.clock(job.estimated_start_ms)} 开始`;
    const finish = known(job) && !view.paused ? ` · ${text.clock(job.estimated_finish_ms)} 完成` : "";
    const body = node("div");
    body.append(node("strong", "", job.own ? "我的订单 · " + job.title : "其他订单"), node("span", "", `${text.spec(job)} · ${job.sheets} 面`));
    row.append(node("b", "", index + 1), body, node("small", "", start + finish));
    return row;
  }));
}

// ---------- document and options ----------
function options() {
  return {pages: integer("pages", 1, 1000), copies: integer("copies", 1, 100), paper: radio("paper"), color: radio("color") === "color",
    duplex: radio("duplex") === "double", service: radio("service"), payment: radio("payment"), student: $("student").value.trim()};
}

async function chooseFiles(fileList) {
  const incoming = Array.from(fileList || []);
  if (!incoming.length) return;
  if (docs.length + incoming.length > MAX_DOCS) { say(`最多合并 ${MAX_DOCS} 个文件（已选 ${docs.length} 个）。`, "error"); $("file").value = ""; return; }
  for (const file of incoming) {
    const extension = extensionOf(file.name);
    if (!EXTENSIONS.includes(extension)) { say(`「${file.name}」不是支持的文件格式。`, "error"); $("file").value = ""; return; }
    if (!file.size || file.size > MAX_FILE) { say(`「${file.name}」为空或超过 10 MiB。`, "error"); $("file").value = ""; return; }
  }
  $("file").value = "";
  $("file-card").classList.add("has-file");
  $("remove-file").hidden = false;
  $("choose-label").firstChild.textContent = "加选文件";
  $("upload-progress").hidden = false;
  $("upload-progress").value = 0;
  uploadToken += 1;
  const token = uploadToken;
  uploadCount += 1;
  updateActions();
  const total = incoming.length;
  let index = 0;
  for (const file of incoming) {
    if (token !== uploadToken) return;
    const extension = extensionOf(file.name);
    const doc = {record: null, url: IMAGE_TYPES.includes(extension) ? URL.createObjectURL(file) : null, lines: null, pages: 0};
    if (TEXT_TYPES.includes(extension)) doc.lines = decodeText(await file.arrayBuffer()).split(/\r\n|\r|\n/);
    try {
      const record = await uploadOne(file, index, total, token);
      if (token !== uploadToken) { if (doc.url) URL.revokeObjectURL(doc.url); return; }
      doc.record = record;
      doc.pages = record.pages;
      docs.push(doc);
      aspects.clear();
      resetBook();
      rotation = 0;
      persistDocs();
      index += 1;
      renderFileList();
      recalcPages();
      renderProof();
    } catch (error) {
      if (doc.url) URL.revokeObjectURL(doc.url);
      say(error.message || "上传失败，请重试。", "error");
      break;
    }
  }
  if (token !== uploadToken) return;
  uploadCount = Math.max(0, uploadCount - 1);
  $("upload-progress").hidden = true;
  if (!docs.length) { clearDocument(true); return; }
  if (uploading === null || uploadCount === 0) uploading = null;
  syncPages();
  scheduleQuote();
  updateActions();
  pollConversion();
  say(docs.length > 1 ? `已上传 ${docs.length} 个文件，确认设置后合并为一单。` : "文件已上传，确认打印设置后下单。", "ready");
}

function uploadOne(file, index, total, token) {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    activeRequests.add(request);
    uploading = request;
    request.open("POST", "/api/upload");
    request.setRequestHeader("X-CSRF-Token", csrf);
    request.setRequestHeader("X-File-Name", encodeURIComponent(file.name));
    request.setRequestHeader("Content-Type", "application/octet-stream");
    request.upload.addEventListener("progress", event => {
      if (token !== uploadToken || !event.lengthComputable) return;
      const overall = Math.round(((index + event.loaded / event.total) / total) * 100);
      $("upload-progress").value = overall;
      $("file-info").textContent = `正在上传 ${index + 1}/${total} · ${overall}%`;
    });
    request.addEventListener("loadend", () => {
      activeRequests.delete(request);
      if (uploading === request) uploading = null;
      if (token !== uploadToken) { reject(new Error("上传已取消。")); return; }
      let result = {};
      try { result = JSON.parse(request.responseText); } catch { /* reported below */ }
      if (request.status === 201) resolve(result);
      else reject(new Error(request.status ? result.error || "上传失败，请重试。" : "上传中断，请检查网络后重新选择文件。"));
    });
    request.send(file);
  });
}

function persistDocs() {
  storage.set("sessionStorage", "paperroom.upload", docs.length ? JSON.stringify(docs.map(doc => doc.record.upload_id)) : null);
}

function decodeText(buffer) {
  try { return new TextDecoder("utf-8", {fatal: true}).decode(buffer); } catch { /* not UTF-8 */ }
  try { return new TextDecoder("gb18030").decode(buffer); } catch { return new TextDecoder().decode(buffer); }
}

function loadDocText(doc) {
  if (doc.textLoading || doc.textFailed) return;
  doc.textLoading = true;
  fetch(`/api/document/${doc.record.upload_id}`, {credentials: "same-origin", cache: "no-store"})
    .then(response => response.ok ? response.arrayBuffer() : Promise.reject(new Error("text load")))
    .then(buffer => { doc.lines = decodeText(buffer).split(/\r\n|\r|\n/); })
    .catch(() => { doc.textFailed = true; })
    .finally(() => { doc.textLoading = false; renderProof(); });
}

function fileMeta(doc) {
  const record = doc.record;
  const size = record.size >= 1048576 ? `${(record.size / 1048576).toFixed(1)} MiB` : `${Math.max(1, Math.round(record.size / 1024))} KiB`;
  const pages = record.status === "converting" ? (record.queue_position > 0 ? `前面 ${record.queue_position} 个` : "正在转换预览…")
    : record.pages_locked ? `共 ${record.pages} 页` : `${docPages(doc)} 页 · 待确认`;
  return `${size} · ${pages}`;
}

function renderFileList() {
  const list = $("file-list");
  if (!list) return;
  if (!docs.length) { list.hidden = true; list.replaceChildren(); return; }
  list.hidden = false;
  list.replaceChildren(...docs.map((doc, position) => {
    const row = node("li", "file-row");
    const text = node("div", "file-row-text");
    text.append(node("strong", "", doc.record ? doc.record.name : "上传中…"), node("span", "", doc.record ? fileMeta(doc) : " "));
    const remove = node("button", "file-row-remove", "移除");
    remove.type = "button";
    remove.addEventListener("click", () => removeDoc(position));
    row.append(text, remove);
    return row;
  }));
  const first = docs[0].record;
  $("file-name").textContent = docs.length > 1 ? `${first.name} 等${docs.length}份` : first.name;
  $("file-info").textContent = docs.length > 1 ? `共 ${docs.length} 个文件 · 合计 ${recalcPages()} 页` : fileMeta(docs[0]);
  $("page-note").textContent = docs.length > 1 ? "多文件将合并为一单，合计页数为各文件之和。" : first.note;
  $("choose-label").firstChild.textContent = docs.length > 1 ? "加选文件" : "更换文件";
}

function applyUpload(record, fresh) {
  const existing = docs.find(doc => doc.record && doc.record.upload_id === record.upload_id);
  if (existing) { existing.record = record; if (record.status !== "converting") existing.pages = record.pages; }
  else docs.push({record, url: null, lines: null, pages: record.pages});
  if (fresh) { aspects.clear(); resetBook(); rotation = 0; }
  persistDocs();
  $("file-card").classList.add("has-file");
  $("remove-file").hidden = false;
  renderFileList();
  syncPages();
  clearTimeout(conversionTimer);
  pollConversion();
  renderProof();
  scheduleQuote();
  updateActions();
}

function removeDoc(index) {
  const doc = docs[index];
  if (!doc) return;
  if (doc.url) URL.revokeObjectURL(doc.url);
  docs.splice(index, 1);
  persistDocs();
  if (!docs.length) { clearDocument(false); return; }
  aspects.clear();
  resetBook();
  renderFileList();
  syncPages();
  renderProof();
  scheduleQuote();
  updateActions();
}

async function pollConversion() {
  clearTimeout(conversionTimer);
  if (!docs.some(doc => doc.record && doc.record.status === "converting")) return;
  for (const doc of docs) {
    if (!doc.record || doc.record.status !== "converting") continue;
    try {
      const record = await api(`/api/upload/${doc.record.upload_id}`);
      if (record.upload_id === doc.record.upload_id) applyUpload(record, false);
    } catch { /* retry below */ }
  }
  if (docs.some(doc => doc.record && doc.record.status === "converting")) conversionTimer = setTimeout(pollConversion, 1500);
  else say(docs.every(doc => !doc.record || doc.record.preview === "pdf" || doc.record.pages_locked) ? "文档已就绪，确认打印设置后下单。" : "有文件未能自动识别页数，请手动确认。", "ready");
}

function lockPages(locked) {
  $("pages").readOnly = locked;
  for (const button of document.querySelectorAll('.stepper[data-for="pages"] button')) button.disabled = locked;
  $("pages").closest(".stepper").classList.toggle("locked", locked);
}

function clearDocument(keepMessage) {
  uploadToken += 1;
  uploading = null;
  uploadCount = 0;
  for (const request of activeRequests) request.abort();
  activeRequests.clear();
  clearTimeout(conversionTimer);
  for (const doc of docs) if (doc.url) URL.revokeObjectURL(doc.url);
  docs = [];
  persistDocs();
  $("file").value = "";
  $("file-name").textContent = "选择要打印的文件";
  $("file-info").textContent = "PDF / Word / PPT / Excel / 图片 / 文本 · 单个最大 10 MiB · 一次最多 10 个";
  $("page-note").textContent = "页数按文档自动识别；无法识别时请手动填写。";
  $("choose-label").firstChild.textContent = "选择文件";
  $("remove-file").hidden = true;
  $("file-card").classList.remove("has-file");
  $("upload-progress").hidden = true;
  renderFileList();
  lockPages(false);
  $("pages").value = 8;
  aspects.clear();
  resetBook();
  rotation = 0;
  if (!keepMessage) say("已移除文件。重新选择后即可下单。");
  renderProof();
  scheduleQuote();
}

// ---------- paper preview ----------
function printedPages() { return integer("pages", 1, 1000); }
function sheetCount() { return options().duplex ? Math.ceil(printedPages() / 2) : printedPages(); }
function pageOn(sheet, back) {
  const page = options().duplex ? (sheet - 1) * 2 + (back ? 2 : 1) : back ? 0 : sheet;
  return page <= printedPages() ? page : 0;
}

function previewUrl(page, width) {
  const {doc, local} = locate(page);
  return `/api/preview/${doc.record.upload_id}/${local}?w=${width}`;
}
function previewWidth(pixels) {
  return PREVIEW_WIDTHS.find(width => width >= pixels) || PREVIEW_WIDTHS[PREVIEW_WIDTHS.length - 1];
}
const pdfReadyAt = page => { const {doc, local} = locate(page); return !!doc && !!doc.record && docPages(doc) >= local && recordReady(doc.record); };

function sampleFace(page) {
  const face = node("div", page === 1 ? "sample cover" : "sample body");
  if (page === 1) {
    const art = node("div", "art");
    art.append(node("i", "sun"), node("i", "block"));
    const title = node("h4");
    title.append("操作系统", document.createElement("br"), "实验报告");
    face.append(node("small", "", "2024 年秋季学期"), title, node("p", "", "OPERATING SYSTEMS LABORATORY REPORT"), art,
      node("footer", "", "计算机学院 · OS REPORT"));
  } else {
    face.append(node("small", "", `实验内容 / ${page}`), node("h4", "", SAMPLE_HEADINGS[(page - 2) % SAMPLE_HEADINGS.length]));
    for (let line = 0; line < 12; line += 1) face.append(node("i", line % 5 === 4 ? "line short" : "line"));
    if (page % 3 === 2) {
      const diagram = node("div", "diagram");
      diagram.append(node("span", "", "就绪态"), node("span", "", "运行态"), node("span", "", "阻塞态"));
      face.append(diagram);
    }
  }
  return face;
}

function placeholderFace(page, message) {
  const face = node("div", "placeholder");
  const name = primaryRecord() ? primaryRecord().name : "";
  face.append(node("strong", "", name), node("span", "", `第 ${page} 页`), node("small", "", message));
  return face;
}

function faceFor(page, pixels) {
  if (page === 0) return node("div", "blank", "背面留白");
  if (!docs.length && !uploadCount) return sampleFace(page);
  const {doc, local} = locate(page);
  if (!doc || !doc.record) return placeholderFace(page, uploadCount ? "正在上传…" : "重新选择文件可查看预览");
  if (recordReady(doc.record)) {
    const image = node("img", "page-image");
    image.alt = `第 ${page} 页`;
    image.decoding = "async";
    image.addEventListener("load", () => {
      const aspect = image.naturalHeight / image.naturalWidth;
      if (aspect && Math.abs((aspects.get(page) || PORTRAIT) - aspect) > .01) { aspects.set(page, aspect); layoutSheet(); }
    });
    image.addEventListener("error", () => image.replaceWith(placeholderFace(page, "预览加载失败，可稍后重试")));
    image.src = previewUrl(page, previewWidth(pixels));
    return image;
  }
  if (doc.record.kind === "image" && local === 1) {
    const image = node("img", "photo");
    image.alt = doc.record.name;
    image.addEventListener("load", () => { aspects.set(page, image.naturalWidth > image.naturalHeight ? 1 / PORTRAIT : PORTRAIT); layoutSheet(); });
    image.addEventListener("error", () => image.replaceWith(placeholderFace(page, "图片预览加载失败，可稍后重试")));
    image.src = doc.url || `/api/document/${doc.record.upload_id}`;
    return image;
  }
  if (doc.record.kind === "text") {
    if (doc.lines) return node("pre", "text-page", doc.lines.slice((local - 1) * 50, local * 50).join("\n"));
    loadDocText(doc);
    return placeholderFace(page, doc.textFailed ? "文本预览加载失败，可稍后重试" : "正在载入文本…");
  }
  const record = doc.record;
  const message = uploadCount ? "正在上传…" : record.status === "converting" ? "正在转换预览…"
    : record.kind === "pdf" ? "展示电脑未启用 PDF 预览" : record.preview === "none" ? "此格式暂无预览" : "重新选择文件可查看预览";
  return placeholderFace(page, message);
}

// A turn is one physical leaf crossing the spine. Zero is the centered cover;
// count is the centered back cover. Missing printed backs remain real blanks.
function spreadAt(turn) {
  const count = sheetCount();
  return {
    left: turn > 0 ? pageOn(turn, true) : null,
    right: turn < count ? pageOn(turn + 1, false) : null,
    state: turn === 0 ? "cover" : turn === count ? "back-cover" : "open",
  };
}
function reducedBookMotion() {
  return document.body.classList.contains("reduce-motion") || matchMedia("(prefers-reduced-motion: reduce)").matches;
}
function stopBookMotion() {
  if (!bookMotion) return;
  const animation = bookMotion.animation;
  bookMotion = null;
  if (animation) animation.cancel();
  $("sheet").hidden = true;
  $("book").dataset.turning = "false";
  $("book").dataset.loading = "false";
  $("stage").setAttribute("aria-busy", "false");
}
function resetBook() {
  stopBookMotion();
  bookTurn = bookTarget = 0;
}
function bookPixels() {
  return Math.max(240, $("book").clientWidth / 2) * (window.devicePixelRatio || 1);
}
function prepareBookPaper(page) {
  if (page === null) return {page, content: null};
  const content = node("div", "book-content");
  content.append(faceFor(page, bookPixels()));
  return {page, content};
}
function fillPaper(id, page, content) {
  const paper = $(id);
  paper.hidden = page === null;
  paper.dataset.page = page === null ? "" : String(page);
  if (page !== null) paper.replaceChildren(content || faceFor(page, bookPixels()));
}
function paintBook(prepared) {
  const spread = spreadAt(bookTurn);
  fillPaper("book-left", spread.left, prepared && prepared.left.content);
  fillPaper("book-right", spread.right, prepared && prepared.right.content);
  $("sheet").hidden = true;
  $("book").dataset.turn = String(bookTurn);
  $("book").dataset.turning = "false";
}
function updateBookControls() {
  const count = sheetCount(), spread = spreadAt(bookTurn);
  const visible = [spread.left, spread.right].filter(page => page !== null && page !== 0);
  const label = spread.state === "cover" ? "封面" : spread.state === "back-cover" ? "封底" : visible.map(page => `第 ${page} 页`).join(" / ");
  $("proof-index").textContent = spread.state === "open" ? `${visible.join("–")} / ${printedPages()}` : label;
  $("proof-index").setAttribute("aria-label", `${label}，共 ${printedPages()} 页`);
  $("proof-prev").disabled = bookTarget <= 0;
  $("proof-next").disabled = bookTarget >= count;
  $("proof-flip").disabled = bookTarget === 0;
  $("proof-rotate").disabled = !hasDocs();
  $("proof-caption").textContent = hasDocs() || uploadCount
    ? `${label}${spread.left === 0 ? " · 背面留白" : ""} · 共 ${printedPages()} 页 / ${count} 张纸`
    : "示例文档 · 左右滑动翻开，双击页面放大";
  if (bookMotion && !bookMotion.animation) $("proof-caption").textContent += " · 正在准备页面…";
}
function warmBookPages() {
  for (let leaf = bookTurn + 1; leaf <= Math.min(bookTurn + 2, sheetCount()); leaf += 1) {
    for (const back of [false, true]) {
      const page = pageOn(leaf, back);
      if (page && pdfReadyAt(page)) new Image().src = previewUrl(page, previewWidth(bookPixels()));
    }
  }
}
function renderProof() {
  // Settings, removal and conversion completion invalidate an in-flight leaf.
  // Commit the latest intent and discard its callback before rebuilding faces.
  stopBookMotion();
  if (docs.length) recalcPages();
  bookTurn = bookTarget = Math.min(Math.max(bookTarget, 0), sheetCount());
  $("book").classList.toggle("mono", !options().color);
  paintBook();
  updateBookControls();
  updateSummary();
  layoutSheet();
  warmBookPages();
}
function layoutSheet() {
  const stage = $("stage"), width = stage.clientWidth, height = stage.clientHeight;
  if (!width || !height) return;
  const pose = bookMotion && bookMotion.started ? bookMotion.destination : bookTurn;
  const spread = spreadAt(pose);
  // Keep one physical book size while turning mixed portrait/landscape pages.
  // Each page's object-fit contains its original orientation within that size.
  const aspect = aspects.get(1) || PORTRAIT;
  const turned = rotation % 180 !== 0;
  const visual = turned ? 1 / aspect : aspect;
  const visibleWidth = Math.min(width * .78, height * .82 / visual);
  const [paperWidth, paperHeight] = turned ? [visibleWidth * visual, visibleWidth] : [visibleWidth, visibleWidth * visual];
  const book = $("book");
  book.style.width = `${paperWidth * 2}px`;
  book.style.height = `${paperHeight}px`;
  book.style.setProperty("--u", `${paperWidth / 100}px`);
  const spreadWidth = turned ? paperHeight : paperWidth * 2;
  const spreadHeight = turned ? paperWidth * 2 : paperHeight;
  const scale = spread.state === "open" ? Math.min(1, width * .9 / spreadWidth, height * .86 / spreadHeight) : 1;
  const offset = spread.state === "cover" ? -25 : spread.state === "back-cover" ? 25 : 0;
  book.dataset.state = spread.state;
  book.style.transform = `translate(-50%, -50%) scale(${scale}) rotate(${rotation}deg) translateX(${offset}%)`;
}
function animateBook() {
  if (bookMotion || bookTarget === bookTurn) return;
  const from = bookTurn, to = from + Math.sign(bookTarget - from), forward = to > from;
  const leaf = forward ? from + 1 : from;
  const before = spreadAt(from), after = spreadAt(to);
  const prepared = {
    left: prepareBookPaper(forward ? before.left : after.left),
    right: prepareBookPaper(forward ? after.right : before.right),
    front: prepareBookPaper(pageOn(leaf, false)),
    back: prepareBookPaper(pageOn(leaf, true)),
  };
  const motion = {from, to, destination: to, animation: null, started: false};
  bookMotion = motion;
  const begin = () => {
    if (bookMotion !== motion) return;
    // A second gesture can cancel or change direction while a PNG is loading.
    if ((bookTarget - from) * (to - from) <= 0) {
      stopBookMotion();
      updateBookControls();
      animateBook();
      return;
    }
    startBookTurn(motion, prepared, forward);
  };
  const images = Object.values(prepared).flatMap(paper => paper.content ? [...paper.content.querySelectorAll("img")] : []);
  if (!images.length) begin();
  else {
    $("book").dataset.loading = "true";
    $("stage").setAttribute("aria-busy", "true");
    // Keep the current spread visible until both sides and the exposed pages
    // are decoded. Failed images still use faceFor's existing error message.
    Promise.all(images.map(image => image.decode().catch(() => {}))).then(begin);
  }
}
function startBookTurn(motion, prepared, forward) {
  // The turning leaf sits over the two exposed stationary pages. Its back is
  // the same physical sheet, never a mirrored copy of its printed front.
  for (const [id, paper] of [["book-left", prepared.left], ["book-right", prepared.right],
    ["face-front", prepared.front], ["face-back", prepared.back]]) fillPaper(id, paper.page, paper.content);
  const sheet = $("sheet");
  sheet.hidden = false;
  $("book").dataset.loading = "false";
  $("stage").setAttribute("aria-busy", "false");
  $("book").dataset.turning = "true";
  motion.started = true;
  layoutSheet();
  const duration = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--book-duration")) || 280;
  const easing = getComputedStyle(document.documentElement).getPropertyValue("--ease-book").trim();
  motion.animation = sheet.animate([
    {transform: `rotateY(${forward ? 0 : -180}deg)`},
    {transform: `rotateY(${forward ? -180 : 0}deg)`},
  ], {duration, easing, fill: "both"});
  motion.animation.onfinish = () => {
    if (bookMotion !== motion) return;
    bookTurn = motion.destination;
    stopBookMotion();
    const endedForward = (bookTurn === motion.to) === forward;
    paintBook({left: endedForward ? prepared.back : prepared.left, right: endedForward ? prepared.right : prepared.front});
    updateBookControls();
    layoutSheet();
    animateBook();
    warmBookPages();
  };
}
function step(delta, immediate = false) {
  const next = Math.min(Math.max(bookTarget + delta, 0), sheetCount());
  if (next === bookTarget) return;
  bookTarget = next;
  if (immediate || reducedBookMotion()) {
    const book = $("book");
    if (immediate) book.classList.add("silent");
    renderProof();
    if (immediate) {
      book.getBoundingClientRect(); // Commit the keyboard pose without a CSS transition.
      requestAnimationFrame(() => book.classList.remove("silent"));
    }
    return;
  }
  if (bookMotion && !bookMotion.animation && (bookTarget - bookMotion.from) * (bookMotion.to - bookMotion.from) <= 0) {
    stopBookMotion();
    animateBook();
  } else if (bookMotion && bookMotion.animation) {
    const motion = bookMotion;
    const returns = (bookTarget - motion.from) * (motion.to - motion.from) <= 0;
    const destination = returns ? motion.from : motion.to;
    if (destination !== motion.destination) {
      motion.destination = destination;
      motion.animation.reverse();
      layoutSheet();
    }
  } else if (!bookMotion) animateBook();
  updateBookControls();
}
function focusedBookPage() {
  const spread = spreadAt(bookTurn);
  return spread.right || spread.left || printedPages();
}

function openReader(page) {
  readerPage = Math.min(Math.max(page || 1, 1), printedPages());
  renderReader();
  if (!$("reader").open) $("reader").showModal();
}
function renderReader() {
  const pixels = Math.min(window.innerWidth, 900) * (window.devicePixelRatio || 1);
  const face = faceFor(readerPage, pixels);
  const paper = node("div", "reader-paper" + (options().color ? "" : " mono") + (face.tagName === "IMG" ? " fluid" : ""));
  paper.style.setProperty("--u", `${Math.min(window.innerWidth - 32, 760) / 100}px`);
  paper.append(face);
  $("reader-page").replaceChildren(paper);
  $("reader-title").textContent = primaryRecord() ? primaryRecord().name : "示例文档";
  $("reader-index").textContent = `第 ${readerPage} / ${printedPages()} 页`;
  $("reader-prev").disabled = readerPage <= 1;
  $("reader-next").disabled = readerPage >= printedPages();
}

// ---------- quote and checkout ----------
function scheduleQuote() {
  quoteState = "pending";
  quoteSequence += 1;
  const sequence = quoteSequence;
  clearTimeout(quoteTimer);
  quoteTimer = setTimeout(() => requestQuote(sequence), 350);
  updateActions();
}

async function requestQuote(sequence) {
  clearTimeout(quoteRefresh);
  const order = options();
  if (!view) return;
  if (order.pages * order.copies > 10000) {
    quote = null; quoteState = "invalid"; renderQuote(); return;
  }
  try {
    const result = await api("/api/quote", orderBody(order));
    if (sequence !== quoteSequence) return;
    quote = result;
    quoteState = result.type === "quote" ? "ok" : "error";
    quoteError = "当前没有支持该规格的设备，无法下单";
  } catch (error) {
    if (sequence !== quoteSequence) return;
    quote = null;
    quoteState = "error";
    quoteError = error.message;
  }
  renderQuote();
  // Waits move with the queue; refresh them while the student looks at the order page.
  quoteRefresh = setTimeout(() => { if (!document.hidden && document.body.dataset.page === "order") scheduleQuote(); }, 30000);
}

function quoteWording() {
  if (quoteState !== "ok") return [quoteState === "invalid" ? "总面数（页数 × 份数）不得超过 10000" : quoteState === "pending" ? "正在向店铺确认报价…" : quoteError, "", quoteState === "pending" ? "" : "error"];
  if (known(quote) && !quote.paused) {
    const minutes = Math.max(1, Math.ceil((quote.estimated_finish_ms - quote.now_ms) / 60000));
    return [minutes === 1 ? "预计 1 分钟内可取件" : `预计 ${minutes} 分钟后可取件`, `前面 ${quote.jobs_ahead} 单 · 按当前队列估算 · 承诺 ${text.date(quote.deadline_ms)} 前可取件`, "ready"];
  }
  return [text.quoteForecast(quote), `承诺 ${text.date(quote.deadline_ms)} 前可取件。预计时间按当前队列推算，不含之后到达的订单。`, "waiting"];
}

function renderQuote() {
  for (const price of document.querySelectorAll("[data-tier]")) {
    const cents = quote && quote.tiers ? quote.tiers[price.dataset.tier] : undefined;
    price.textContent = typeof cents === "number" ? text.money(cents) : "—";
  }
  $("price").textContent = quoteState === "ok" ? text.money(quote.quoted_cents) : "¥—";
  const [eta, detail, tone] = quoteWording();
  $("eta").textContent = eta;
  $("eta").className = tone;
  $("eta-detail").textContent = detail;
  updateActions();
}

function updateSummary() {
  const order = options();
  $("summary").textContent = `${order.pages} 页 / ${order.paper.toUpperCase()} / ${order.color ? "彩色" : "黑白"} / ${order.duplex ? "双面" : "单面"}`;
}

function blocker() {
  if (!connected) return "店铺暂时连不上，恢复连接后即可支付。";
  if (view && !view.shop_enabled) return "这个数据目录不是经营模式，无法下单。";
  if (uploadCount || uploading) return "正在上传文件…";
  if (!hasDocs()) return "先选择要打印的文件。";
  if (docs.some(doc => doc.record && doc.record.status === "converting")) return "正在准备文档预览，完成后即可下单。";
  if (quoteState === "ok") return "";
  if (quoteState === "pending") return "正在向店铺确认报价…";
  return "当前设置暂时无法下单，原因见上方说明。";
}

function updateActions() {
  const reason = blocker();
  $("pay").disabled = Boolean(reason);
  $("pay-hint").textContent = reason || "确认后进入模拟支付。";
  $("file").disabled = !connected;
  $("choose-label").classList.toggle("disabled", !connected);
}

// ---------- payment ----------
function openPayment() {
  if (blocker()) return;
  const order = options();
  $("pay-total").textContent = text.money(quote.quoted_cents);
  const name = primaryRecord() ? primaryRecord().name : "";
  $("pay-document").textContent = docs.length > 1 ? `${name} 等${docs.length}份` : name;
  $("pay-spec").textContent = `${text.spec(order)} · ${order.pages} 页 × ${order.copies} 份`;
  $("pay-fee").textContent = `${order.pages * order.copies} 面 · ${quote.paper_sheets} 张纸 · 原价 ${text.money(quote.base_cents)} · ${DISCOUNTS[order.service]}${order.duplex ? " · 双面 95 折" : ""}`;
  $("pay-pickup").textContent = $("eta").textContent;
  $("pay-status").textContent = "";
  $("pay-status").className = "pay-status";
  setPaying(false);
  renderReceipt();
  $("pay-dialog").showModal();
}

function renderReceipt() {
  const method = radio("payment"), label = PAYMENTS[method];
  const configured = view && view.features.payment_codes.includes(method);
  const box = $("receipt-code");
  if (configured) {
    const image = node("img");
    image.alt = `${label}收款码`;
    image.src = `/payment-code/${method}`;
    box.replaceChildren(image);
  } else {
    box.replaceChildren(node("span", "", method === "campus_card" ? "刷卡区预留" : "收款码待配置"));
  }
  $("receipt-title").textContent = method === "campus_card" ? "校园卡支付" : `${label}收款码`;
  $("receipt-text").textContent = configured ? "展示者配置的收款码。应用内“确认支付”为模拟；扫码转账以支付平台实际账单为准。"
    : method === "campus_card" ? "到店刷卡区预留。课堂演示中点击“确认支付”即可模拟付款。"
      : `此处预留${label}收款码。课堂演示无需扫码，点击“确认支付”即可模拟付款。`;
}

function setPaying(busy) {
  $("pay-confirm").disabled = busy;
  $("pay-confirm").textContent = busy ? "提交中…" : "确认支付";
  for (const input of document.querySelectorAll('input[name="payment"]')) input.disabled = busy;
}

async function confirmPayment() {
  if (!hasDocs()) return;
  setPaying(true);
  $("pay-status").textContent = "支付处理中…";
  try {
    const result = await api("/api/submit", orderBody(options()));
    $("pay-dialog").close();
    highlight = result.job_id;
    clearDocument(true);
    const message = `订单已接收（模拟支付）。取件码 ${result.pickup_code}，排队、预计取件和打印进度将在本页自动更新。`;
    say(message, "ready");
    toast(message);
    location.hash = "#mine";
    await poll();
  } catch (error) {
    setPaying(false);
    $("pay-status").textContent = error.status >= 400 && error.status < 500 ? "订单未提交，未扣款：" + error.message : error.message;
    $("pay-status").className = "pay-status error";
    poll();
  }
}

function askCancel(job) {
  cancelTarget = job;
  $("cancel-text").textContent = (job.state === "running" ? "订单正在打印，取消后会停止，已打印的部分作废。" : "订单还在排队，取消后立即退出队列。")
    + ` ${text.money(job.quoted_cents)} 将按原路退回（模拟）。`;
  $("cancel-confirm").disabled = false;
  $("cancel-dialog").showModal();
}

async function confirmCancel() {
  if (!cancelTarget) return;
  $("cancel-confirm").disabled = true;
  try {
    await api("/api/cancel", {job_id: cancelTarget.job_id});
    $("cancel-dialog").close();
    say("已提交取消；停止打印后按原路退款（模拟）。", "ready");
    toast("已提交取消；停止打印后按原路退款（模拟）。");
  } catch (error) {
    $("cancel-dialog").close();
    say(error.message, "error");
  }
  cancelTarget = null;
  poll();
}

// ---------- wiring ----------
function setMotion(reduced) {
  document.body.classList.toggle("reduce-motion", reduced);
  $("motion").setAttribute("aria-pressed", String(reduced));
  $("motion").textContent = reduced ? "恢复动效" : "减少动效";
  if (reduced && bookMotion) renderProof();
}

function wire() {
  window.addEventListener("hashchange", showPage);
  $("file").addEventListener("change", () => chooseFiles($("file").files));
  $("remove-file").addEventListener("click", () => clearDocument(false));
  for (const stepper of document.querySelectorAll(".stepper")) {
    const input = $(stepper.dataset.for);
    stepper.addEventListener("click", event => {
      const button = event.target.closest("button[data-step]");
      if (!button || input.readOnly) return;
      input.value = integer(input.id, Number(input.min), Number(input.max)) + Number(button.dataset.step);
      input.value = integer(input.id, Number(input.min), Number(input.max));
      renderProof();
      scheduleQuote();
    });
    input.addEventListener("input", () => { renderProof(); scheduleQuote(); });
    input.addEventListener("change", () => { input.value = integer(input.id, Number(input.min), Number(input.max)); renderProof(); });
  }
  for (const name of ["paper", "color", "duplex", "service"]) {
    for (const input of document.querySelectorAll(`input[name="${name}"]`)) input.addEventListener("change", () => { renderProof(); scheduleQuote(); });
  }
  for (const input of document.querySelectorAll('input[name="payment"]')) input.addEventListener("change", renderReceipt);
  $("student").value = storage.get("localStorage", "paperroom.student") || "";
  $("student").addEventListener("input", () => {
    storage.set("localStorage", "paperroom.student", $("student").value.trim() || null);
    if (view) renderOrders();
  });
  $("proof-prev").addEventListener("click", () => step(-1));
  $("proof-next").addEventListener("click", () => step(1));
  $("proof-flip").addEventListener("click", () => step(-bookTarget));
  $("proof-rotate").addEventListener("click", () => { rotation = (rotation + 90) % 360; layoutSheet(); });
  $("proof-zoom").addEventListener("click", () => openReader(focusedBookPage()));
  const stage = $("stage");
  let start = null;
  stage.addEventListener("pointerdown", event => {
    if (event.isPrimary && event.button === 0) start = {x: event.clientX, y: event.clientY};
  });
  stage.addEventListener("pointercancel", () => { start = null; });
  stage.addEventListener("pointerup", event => {
    if (!start) return;
    const dx = event.clientX - start.x, dy = event.clientY - start.y;
    start = null;
    if (Math.abs(dx) > 45 && Math.abs(dx) > Math.abs(dy) * 1.4) step(dx < 0 ? 1 : -1);
  });
  stage.addEventListener("dblclick", event => {
    const paper = event.target.closest(".book-paper");
    openReader(paper && Number(paper.dataset.page) || focusedBookPage());
  });
  stage.addEventListener("keydown", event => {
    if (event.key === "ArrowLeft" || event.key === "PageUp") { event.preventDefault(); step(-1, true); }
    else if (event.key === "ArrowRight" || event.key === "PageDown") { event.preventDefault(); step(1, true); }
    else if (event.key === "Enter") { event.preventDefault(); $("proof-zoom").click(); }
  });
  $("reader-prev").addEventListener("click", () => { readerPage -= 1; renderReader(); });
  $("reader-next").addEventListener("click", () => { readerPage += 1; renderReader(); });
  $("pay").addEventListener("click", openPayment);
  $("pay-confirm").addEventListener("click", confirmPayment);
  $("cancel-confirm").addEventListener("click", confirmCancel);
  for (const dialog of document.querySelectorAll("dialog")) {
    // Tapping the dimmed backdrop closes sheets, like native bottom sheets.
    dialog.addEventListener("click", event => { if (event.target === dialog && !$("pay-confirm").disabled) dialog.close(); });
  }
  matchMedia("(prefers-reduced-motion: reduce)").addEventListener("change", event => {
    if (event.matches && bookMotion) renderProof();
  });
  const stored = storage.get("localStorage", "paperroom.motion");
  setMotion(stored === null ? matchMedia("(prefers-reduced-motion: reduce)").matches : stored === "1");
  $("motion").addEventListener("click", () => {
    const reduced = !document.body.classList.contains("reduce-motion");
    setMotion(reduced);
    storage.set("localStorage", "paperroom.motion", reduced ? "1" : "0");
  });
  if ("ResizeObserver" in window) new ResizeObserver(layoutSheet).observe(stage); else window.addEventListener("resize", layoutSheet);
  document.addEventListener("visibilitychange", () => { if (document.hidden) clearTimeout(pollTimer); else poll(); });
}

async function restoreUpload() {
  // Mobile browsers may reload the page after the file picker; keep the uploads.
  const raw = storage.get("sessionStorage", "paperroom.upload");
  if (!raw) return;
  let identifiers;
  try { identifiers = JSON.parse(raw); } catch { identifiers = null; }
  if (!Array.isArray(identifiers) || !identifiers.length || !identifiers.every(id => /^[a-f0-9]{32}$/.test(id))) {
    storage.set("sessionStorage", "paperroom.upload", null);
    return;
  }
  try {
    for (const identifier of identifiers) applyUpload(await api(`/api/upload/${identifier}`), false);
    if (docs.length) { aspects.clear(); rotation = 0; renderFileList(); syncPages(); renderProof(); }
  } catch { storage.set("sessionStorage", "paperroom.upload", null); }
}

wire();
showPage();
renderProof();
renderQuote();
poll().then(restoreUpload);
