// イコライザーの画面。ツマミを回すたびに、Rust 側の set_state で設定ファイルを書く。
// APO は設定ファイルの変更を見て、すぐ音に反映する。
"use strict";

const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

const powerButton = document.getElementById("power");
const powerLabel = document.getElementById("power-label");
const resetButton = document.getElementById("reset");
const viewAnalogButton = document.getElementById("view-analog");
const viewDigitalButton = document.getElementById("view-digital");
const viewCurveButton = document.getElementById("view-curve");
const noticeElement = document.getElementById("notice");
const noticeText = document.getElementById("notice-text");
const attachButton = document.getElementById("attach");
const statusElement = document.getElementById("status");

// ツマミは、まんなか（真上）から左右に 135 度ずつ回る。
const SWEEP_DEGREES = 270;
// ツマミを端から端まで回すのに要る、マウスの横の移動量（px）。
const DRAG_PIXELS = 220;

let state = { enabled: false, treble: 0, mid: 0, bass: 0, volume: 100 };
// いまの出力デバイスの状態（Rust 側の DeviceStatus）。
let device = null;
let attaching = false;
let attachError = "";
const knobs = {};

function formatDb(value) {
  const rounded = Math.round(value * 10) / 10;
  return (rounded > 0 ? "+" : rounded < 0 ? "−" : "") + Math.abs(rounded).toFixed(1) + " dB";
}

function formatPercent(value) {
  return Math.round(value) + "%";
}

// ---- 曲線の表示 ----
// 横が周波数、縦が増減（dB）のグラフ。線は、実際にかかるフィルタを計算した形。
// 後ろの LED の棒は、その周波数での線の高さまで、下から光る。

// グラフの大きさ（px）。index.html の #curve と合わせる。range と tone は setupCurve が決める。
const CURVE = {
  width: 392,
  top: 2,
  plotHeight: 146,
  minHz: 20,
  decades: 3, // 20 Hz 〜 20 kHz
  sampleRate: 48000, // 線を描くための値。実際のデバイスと違っても、形はほとんど変わらない
  bars: 20,
  cells: 20,
  barsLeft: 26, // 左の端は dB の目盛りの数字のために空ける
  barsRight: 4,
  tone: 20,
  range: 22,
};

// フィルタの係数。APO（apo/src/Dsp.cpp）と同じ式。返すのは [b0, b1, b2, a1, a2]。
function peakingFilter(hz, db, q) {
  const a = Math.pow(10, db / 40);
  const w0 = (2 * Math.PI * hz) / CURVE.sampleRate;
  const cos = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * q);
  const a0 = 1 + alpha / a;
  return [(1 + alpha * a) / a0, (-2 * cos) / a0, (1 - alpha * a) / a0, (-2 * cos) / a0, (1 - alpha / a) / a0];
}

function lowShelfFilter(hz, db, q) {
  const a = Math.pow(10, db / 40);
  const w0 = (2 * Math.PI * hz) / CURVE.sampleRate;
  const cos = Math.cos(w0);
  const k = 2 * Math.sqrt(a) * (Math.sin(w0) / (2 * q));
  const a0 = a + 1 + (a - 1) * cos + k;
  return [
    (a * (a + 1 - (a - 1) * cos + k)) / a0,
    (2 * a * (a - 1 - (a + 1) * cos)) / a0,
    (a * (a + 1 - (a - 1) * cos - k)) / a0,
    (-2 * (a - 1 + (a + 1) * cos)) / a0,
    (a + 1 + (a - 1) * cos - k) / a0,
  ];
}

function highShelfFilter(hz, db, q) {
  const a = Math.pow(10, db / 40);
  const w0 = (2 * Math.PI * hz) / CURVE.sampleRate;
  const cos = Math.cos(w0);
  const k = 2 * Math.sqrt(a) * (Math.sin(w0) / (2 * q));
  const a0 = a + 1 - (a - 1) * cos + k;
  return [
    (a * (a + 1 + (a - 1) * cos + k)) / a0,
    (-2 * a * (a - 1 + (a + 1) * cos)) / a0,
    (a * (a + 1 + (a - 1) * cos - k)) / a0,
    (2 * (a - 1 - (a + 1) * cos)) / a0,
    (a + 1 - (a - 1) * cos - k) / a0,
  ];
}

// フィルタが、その周波数の音を何 dB 上げ下げするか。
function magnitudeDb(c, hz) {
  const w = (2 * Math.PI * hz) / CURVE.sampleRate;
  const cos1 = Math.cos(w);
  const sin1 = Math.sin(w);
  const cos2 = Math.cos(2 * w);
  const sin2 = Math.sin(2 * w);
  const nr = c[0] + c[1] * cos1 + c[2] * cos2;
  const ni = c[1] * sin1 + c[2] * sin2;
  const dr = 1 + c[3] * cos1 + c[4] * cos2;
  const di = c[3] * sin1 + c[4] * sin2;
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

// 3 つのフィルタ。周波数と Q は、設定ファイルに書く値（app/src-tauri/src/settings.rs）と同じ。
// pointHz は、点を置く左右の位置。BASS と TREBLE は、線がほぼ設定の値に届く平らなところに置く
// （フィルタの周波数の 150 Hz、5 kHz では、線は設定の値の半分ほどの高さしかない）。
const CURVE_FILTERS = {
  bass: { name: "BASS", pointHz: 50, make: (db) => lowShelfFilter(150, db, 0.707) },
  mid: { name: "MID", pointHz: 2000, make: (db) => peakingFilter(2000, db, 0.7) },
  treble: { name: "TREBLE", pointHz: 12000, make: (db) => highShelfFilter(5000, db, 0.707) },
};

const SVG_NS = "http://www.w3.org/2000/svg";
const curveSvg = document.getElementById("curve");
const curveLine = document.getElementById("curve-line");
const curveLeds = []; // curveLeds[棒][下からの粒]
let curveReady = false;

function curveX(hz) {
  return (Math.log10(hz / CURVE.minHz) / CURVE.decades) * CURVE.width;
}

function curveHz(x) {
  return CURVE.minHz * Math.pow(10, (x / CURVE.width) * CURVE.decades);
}

function curveY(db) {
  return CURVE.top + ((CURVE.range - db) / (2 * CURVE.range)) * CURVE.plotHeight;
}

// 画面の y 座標を、グラフの上の dB に直す。
function curveDbAt(clientY) {
  const y = clientY - curveSvg.getBoundingClientRect().top;
  return CURVE.range - ((y - CURVE.top) / CURVE.plotHeight) * 2 * CURVE.range;
}

function svgElement(name, attributes, parent, text) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
  if (text !== undefined) element.textContent = text;
  parent.appendChild(element);
  return element;
}

// LED、目盛り、名前を並べる。tone は BASS などの値の範囲（±dB）。
function setupCurve(tone) {
  CURVE.tone = tone;
  CURVE.range = tone * 1.1; // 上下の端に、点の丸がはみ出さないだけの空きを取る
  const bottom = CURVE.top + CURVE.plotHeight;

  const ledGroup = document.getElementById("curve-leds");
  const barWidth = (CURVE.width - CURVE.barsRight - CURVE.barsLeft) / CURVE.bars;
  const cellDb = (2 * tone) / CURVE.cells;
  const cellHeight = curveY(0) - curveY(cellDb);
  for (let bar = 0; bar < CURVE.bars; bar++) {
    const cells = [];
    for (let cell = 0; cell < CURVE.cells; cell++) {
      cells.push(
        svgElement(
          "rect",
          {
            class: "led",
            x: (CURVE.barsLeft + bar * barWidth + 1).toFixed(2),
            y: (curveY(-tone + (cell + 1) * cellDb) + 1).toFixed(2),
            width: (barWidth - 2).toFixed(2),
            height: (cellHeight - 2).toFixed(2),
            rx: 1,
          },
          ledGroup
        )
      );
    }
    curveLeds.push(cells);
  }

  const axis = document.getElementById("curve-axis");
  svgElement("line", { class: "zero", x1: 0, x2: CURVE.width, y1: curveY(0), y2: curveY(0) }, axis);
  for (const db of [tone, 0, -tone]) {
    const text = (db > 0 ? "+" : db < 0 ? "−" : "") + Math.abs(db);
    // いちばん上の数字は、窓からはみ出さないよう、線の下に書く。
    svgElement("text", { class: "axis", x: 5, y: db > 0 ? curveY(db) + 9 : curveY(db) - 3 }, axis, text);
  }
  for (const [hz, text] of [[100, "100"], [1000, "1k"], [10000, "10k"]]) {
    const x = curveX(hz);
    svgElement("line", { class: "tick", x1: x, x2: x, y1: bottom - 5, y2: bottom }, axis);
    svgElement("text", { class: "axis", x: x + 3, y: bottom - 1.5 }, axis, text);
  }
  for (const filter of Object.values(CURVE_FILTERS)) {
    svgElement("text", { class: "name", x: curveX(filter.pointHz), y: bottom + 13 }, axis, filter.name);
  }

  curveReady = true;
}

function renderCurve() {
  if (!curveReady) return;
  const filters = [];
  for (const [key, filter] of Object.entries(CURVE_FILTERS)) {
    if (state[key] !== 0) filters.push(filter.make(state[key]));
  }
  const dbAt = (x) => {
    const hz = curveHz(x);
    let sum = 0;
    for (const coefficients of filters) sum += magnitudeDb(coefficients, hz);
    return sum;
  };

  let path = "";
  for (let x = 0; x <= CURVE.width; x += 2) path += (x ? "L" : "M") + x + " " + curveY(dbAt(x)).toFixed(2);
  curveLine.setAttribute("d", path);

  // LED の棒は、下の端から、棒のまんなかの周波数での線の高さまで光る。
  const barWidth = (CURVE.width - CURVE.barsRight - CURVE.barsLeft) / CURVE.bars;
  const cellDb = (2 * CURVE.tone) / CURVE.cells;
  curveLeds.forEach((cells, bar) => {
    const db = dbAt(CURVE.barsLeft + (bar + 0.5) * barWidth);
    cells.forEach((led, cell) => led.classList.toggle("lit", -CURVE.tone + cell * cellDb < db - 1e-9));
  });

  // 点は、設定の値の高さに置く。数字は点の上（下げているときは下）。端に近いときは反対側。
  for (const [key, filter] of Object.entries(CURVE_FILTERS)) {
    const point = document.getElementById(key + "-point");
    const value = state[key];
    const y = curveY(value);
    point.setAttribute("transform", `translate(${curveX(filter.pointHz).toFixed(2)} ${y.toFixed(2)})`);
    const above = value >= 0 ? y > CURVE.top + 20 : y > CURVE.top + CURVE.plotHeight - 20;
    const number = point.querySelector(".num");
    number.setAttribute("y", above ? -11 : 19);
    number.textContent = formatDb(value).replace(" dB", "");
  }
}

// 調整する値を 1 つ用意する。ツマミ（アナログの表示）、LED のバー（デジタルの表示）、
// グラフの点（曲線の表示。BASS、MID、TREBLE だけ）を受け持つ。
//   key: state のどの値か / min, max, step: 範囲と刻み / reset: ダブルクリックで戻る値
//   detent: この値の近くで止まりやすくする（まんなかの位置など）。無ければ null
//   origin: バーが光りはじめる位置（BASS などは 0 dB、VOLUME は 0%）
//   segments: バーの LED の数 / hotFrom: この値から上の LED は明るい色にする。無ければ null
function setupKnob({ key, min, max, step, reset, detent, format, origin, segments, hotFrom }) {
  const element = document.getElementById(key);
  const pointer = element.querySelector(".pointer");
  const valueElement = document.getElementById(key + "-value");
  const bar = document.getElementById(key + "-bar");
  const numberElement = document.getElementById(key + "-num");
  const unitElement = document.getElementById(key + "-unit");
  const point = document.getElementById(key + "-point"); // VOLUME には無い

  // バーの LED と、まんなか（止まりやすい位置）の印を並べる。
  const segmentSize = (max - min) / segments;
  const leds = [];
  for (let index = 0; index < segments; index++) {
    const led = document.createElement("s");
    bar.appendChild(led);
    leds.push(led);
  }
  if (detent !== null) {
    const mark = document.createElement("u");
    mark.style.left = ((detent - min) / (max - min)) * 100 + "%";
    bar.appendChild(mark);
  }

  const snap = (value) => {
    let snapped = Math.round(value / step) * step;
    if (detent !== null && Math.abs(snapped - detent) <= step) snapped = detent;
    return Math.min(max, Math.max(min, snapped));
  };

  const render = () => {
    const value = state[key];
    const text = format(value);

    const angle = ((value - min) / (max - min) - 0.5) * SWEEP_DEGREES;
    pointer.setAttribute("transform", `rotate(${angle})`);
    valueElement.textContent = text;

    // origin から value までの LED を光らせる。少しでも動いていれば、1 つめが光る。
    const low = Math.min(origin, value);
    const high = Math.max(origin, value);
    leds.forEach((led, index) => {
      const start = min + index * segmentSize;
      const lit = start + segmentSize > low + 1e-9 && start < high - 1e-9;
      led.classList.toggle("lit", lit);
      led.classList.toggle("hot", lit && hotFrom !== null && start >= hotFrom - 1e-9);
    });
    // 数字と単位（"+6.0 dB"、"125%"）を分けて出す。
    const parts = text.match(/^(.*?)\s?(dB|%)$/);
    numberElement.textContent = parts ? parts[1] : text;
    unitElement.textContent = parts ? parts[2] : "";

    for (const target of [element, bar, point]) {
      if (!target) continue;
      target.setAttribute("aria-valuemin", String(min));
      target.setAttribute("aria-valuemax", String(max));
      target.setAttribute("aria-valuenow", String(value));
      target.setAttribute("aria-valuetext", text);
    }
  };

  const change = (value) => {
    const next = snap(value);
    if (next === state[key]) return;
    state[key] = next;
    render();
    renderCurve();
    scheduleSave();
  };

  // バーの上の位置（画面の x 座標）を、値に直す。押した LED までが光る値にする。
  const valueAt = (clientX) => {
    const rect = bar.getBoundingClientRect();
    const width = Math.max(1, rect.width);
    const x = clientX - rect.left;
    // まんなかの印のすぐ近く（3 px 以内）は、まんなかの値にする。
    if (detent !== null && Math.abs(x - ((detent - min) / (max - min)) * width) <= 3) return detent;
    const raw = min + (x / width) * (max - min);
    // origin から遠いほうの刻みに寄せる（押した LED の途中でも、その LED が光るように）。
    const stepped = raw > origin ? Math.ceil(raw / step - 1e-9) * step : Math.floor(raw / step + 1e-9) * step;
    return Math.min(max, Math.max(min, stepped));
  };

  // グラフの上の高さ（画面の y 座標）を、値に直す。
  const valueAtY = (clientY) => {
    const db = curveDbAt(clientY);
    // まんなかの線のすぐ近く（3 px 以内）は、まんなかの値にする。
    if (detent !== null && Math.abs(curveY(db) - curveY(detent)) <= 3) return detent;
    return Math.min(max, Math.max(min, Math.round(db / step) * step));
  };

  const set = (value) => {
    if (value === state[key]) return;
    state[key] = value;
    render();
    renderCurve();
    scheduleSave();
  };

  // ツマミにも、バーにも、グラフの点にも、同じ操作を付ける。違うのはドラッグだけ。
  //   "knob"（ツマミ）: 左右にドラッグした分だけ動く。押した場所へ値が飛ぶことはない。
  //   "bar"（バー）: 押した場所の値になり、ドラッグするとマウスに付いてくる。
  //   "point"（グラフの点）: 点をつかんで上下に動かす。点はマウスに付いてくる。
  const attach = (target, mode) => {
    // 止まりやすい位置を通り抜けられるよう、丸める前の値を覚えておく（ツマミ）。
    let dragStartX = 0;
    let dragStartValue = 0;
    // 点のまんなかから外れた場所をつかんでも、点が飛ばないようにする（グラフの点）。
    let grabOffsetY = 0;
    target.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      target.setPointerCapture(event.pointerId);
      target.classList.add("dragging");
      dragStartX = event.clientX;
      dragStartValue = state[key];
      if (mode === "bar") set(valueAt(event.clientX));
      if (mode === "point") {
        grabOffsetY = event.clientY - (curveSvg.getBoundingClientRect().top + curveY(state[key]));
      }
    });
    target.addEventListener("pointermove", (event) => {
      if (!target.hasPointerCapture(event.pointerId)) return;
      if (mode === "bar") {
        set(valueAt(event.clientX));
        return;
      }
      if (mode === "point") {
        set(valueAtY(event.clientY - grabOffsetY));
        return;
      }
      const fine = event.shiftKey ? 0.25 : 1; // Shift を押している間は細かく動かす
      const delta = ((event.clientX - dragStartX) / DRAG_PIXELS) * (max - min) * fine;
      change(dragStartValue + delta);
    });
    const endDrag = (event) => {
      if (target.hasPointerCapture(event.pointerId)) target.releasePointerCapture(event.pointerId);
      target.classList.remove("dragging");
    };
    target.addEventListener("pointerup", endDrag);
    target.addEventListener("pointercancel", endDrag);

    // ホイールで 1 刻みずつ。
    target.addEventListener(
      "wheel",
      (event) => {
        event.preventDefault();
        change(state[key] + (event.deltaY < 0 ? step : -step));
      },
      { passive: false }
    );

    // ダブルクリックで元の位置へ。
    target.addEventListener("dblclick", () => change(reset));

    // キーボード。
    target.addEventListener("keydown", (event) => {
      const big = step * 5;
      const moves = {
        ArrowUp: step,
        ArrowRight: step,
        ArrowDown: -step,
        ArrowLeft: -step,
        PageUp: big,
        PageDown: -big,
      };
      if (event.key in moves) change(state[key] + moves[event.key]);
      else if (event.key === "Home") change(min);
      else if (event.key === "End") change(max);
      else return;
      event.preventDefault();
    });
  };

  attach(element, "knob");
  attach(bar, "bar");
  if (point) attach(point, "point");

  knobs[key] = { render, reset: () => change(reset) };
}

function render() {
  document.body.classList.toggle("on", state.enabled);
  powerButton.setAttribute("aria-pressed", String(state.enabled));
  powerLabel.textContent = state.enabled ? "ON" : "OFF";
  Object.values(knobs).forEach((knob) => knob.render());
  renderCurve();
}

// 設定ファイルへの書き込み。ツマミを回している間は、まとめて 40 ms に 1 回にする。
let saveTimer = null;
let saving = false;
let dirty = false;

function scheduleSave() {
  dirty = true;
  if (saveTimer !== null) return;
  saveTimer = setTimeout(save, 40);
}

async function save() {
  saveTimer = null;
  if (saving) return; // 前の書き込みが終わったあとに、もう一度書く
  saving = true;
  dirty = false;
  try {
    await invoke("set_state", { state });
    statusElement.textContent = "";
  } catch (error) {
    statusElement.textContent = String(error);
  } finally {
    saving = false;
    if (dirty) scheduleSave();
  }
}

async function load() {
  const payload = await invoke("get_state");
  state = payload.state;

  const tone = payload.maxToneDb;
  setupCurve(tone);
  for (const key of ["bass", "mid", "treble"]) {
    setupKnob({
      key,
      min: -tone,
      max: tone,
      step: 0.5,
      reset: 0,
      detent: 0,
      format: formatDb,
      origin: 0,
      segments: 40,
      hotFrom: null,
    });
  }
  setupKnob({
    key: "volume",
    min: 0,
    max: payload.maxVolumePercent,
    step: 1,
    reset: 100,
    detent: 100,
    format: formatPercent,
    origin: 0,
    segments: 30,
    hotFrom: 100,
  });

  device = payload.device;
  render();
  renderDevice();
}

// いまの出力デバイスにツマミが効かないとき、パネルの上の端で知らせる。
function renderDevice() {
  let text = "";
  let canAttach = false;
  if (device && !device.apoRegistered) {
    text = "The audio component is not installed. Run the installer again.";
  } else if (device && device.deviceId && !device.attached) {
    const name = device.deviceName || "the current output device";
    text = attachError || `Not active on ${name} yet.`;
    canAttach = true;
  }
  noticeElement.hidden = text === "";
  noticeText.textContent = text;
  noticeText.title = text;
  attachButton.hidden = !canAttach;
  attachButton.disabled = attaching;
  attachButton.textContent = attaching ? "Enabling…" : "Enable";
}

// 取り付ける。Windows の確認（UAC）が出て、音が一度止まる。
attachButton.addEventListener("click", async () => {
  if (attaching) return;
  attaching = true;
  attachError = "";
  renderDevice();
  try {
    device = await invoke("attach_device");
  } catch (error) {
    attachError = String(error);
  } finally {
    attaching = false;
    renderDevice();
  }
});

// 出力デバイスが切り替わった、または取り付けの状態が変わったとき。
listen("device-changed", (event) => {
  const changedDevice = !device || device.deviceId !== event.payload.deviceId;
  device = event.payload;
  if (changedDevice) attachError = "";
  renderDevice();
});

powerButton.addEventListener("click", () => {
  state.enabled = !state.enabled;
  render();
  scheduleSave();
});

// ツマミを全部まんなか（0 dB、100%）に戻す。電源の入り切りは変えない。
resetButton.addEventListener("click", () => {
  Object.values(knobs).forEach((knob) => knob.reset());
});

// 表示の切り替え（ツマミ / LED のバー / 曲線）。どれを使うかは、この PC の画面のアプリが覚える。
// 音には関係しないので、設定ファイルには書かない。
const VIEW_KEY = "minitone.view";
const VIEW_BUTTONS = { analog: viewAnalogButton, digital: viewDigitalButton, curve: viewCurveButton };

function setView(view) {
  const current = Object.prototype.hasOwnProperty.call(VIEW_BUTTONS, view) ? view : "analog";
  document.body.classList.toggle("digital", current === "digital");
  document.body.classList.toggle("curve", current === "curve");
  for (const [name, button] of Object.entries(VIEW_BUTTONS)) {
    button.setAttribute("aria-pressed", String(name === current));
  }
  try {
    localStorage.setItem(VIEW_KEY, current);
  } catch (error) {
    // 覚えられなくても、表示の切り替えはできる。
  }
}

function savedView() {
  try {
    return localStorage.getItem(VIEW_KEY);
  } catch (error) {
    return null;
  }
}

viewAnalogButton.addEventListener("click", () => setView("analog"));
viewDigitalButton.addEventListener("click", () => setView("digital"));
viewCurveButton.addEventListener("click", () => setView("curve"));
setView(savedView());

// トレイのメニューから入り切りされたとき。
listen("state-changed", (event) => {
  state = event.payload;
  render();
});

load().catch((error) => {
  statusElement.textContent = String(error);
});
