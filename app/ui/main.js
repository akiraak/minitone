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

// 調整する値を 1 つ用意する。ツマミ（アナログの表示）と LED のバー（デジタルの表示）の両方を受け持つ。
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

    for (const target of [element, bar]) {
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

  const set = (value) => {
    if (value === state[key]) return;
    state[key] = value;
    render();
    scheduleSave();
  };

  // ツマミにもバーにも、同じ操作を付ける。違うのはドラッグだけ。
  //   absolute が false（ツマミ）: 左右にドラッグした分だけ動く。押した場所へ値が飛ぶことはない。
  //   absolute が true（バー）: 押した場所の値になり、ドラッグするとマウスに付いてくる。
  const attach = (target, absolute) => {
    // 止まりやすい位置を通り抜けられるよう、丸める前の値を覚えておく（ツマミ）。
    let dragStartX = 0;
    let dragStartValue = 0;
    target.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      target.setPointerCapture(event.pointerId);
      target.classList.add("dragging");
      dragStartX = event.clientX;
      dragStartValue = state[key];
      if (absolute) set(valueAt(event.clientX));
    });
    target.addEventListener("pointermove", (event) => {
      if (!target.hasPointerCapture(event.pointerId)) return;
      if (absolute) {
        set(valueAt(event.clientX));
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

  attach(element, false);
  attach(bar, true);

  knobs[key] = { render, reset: () => change(reset) };
}

function render() {
  document.body.classList.toggle("on", state.enabled);
  powerButton.setAttribute("aria-pressed", String(state.enabled));
  powerLabel.textContent = state.enabled ? "ON" : "OFF";
  Object.values(knobs).forEach((knob) => knob.render());
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

// 表示の切り替え（ツマミ / LED のバー）。どちらを使うかは、この PC の画面のアプリが覚える。
// 音には関係しないので、設定ファイルには書かない。
const VIEW_KEY = "minitone.view";

function setView(view) {
  const digital = view === "digital";
  document.body.classList.toggle("digital", digital);
  viewAnalogButton.setAttribute("aria-pressed", String(!digital));
  viewDigitalButton.setAttribute("aria-pressed", String(digital));
  try {
    localStorage.setItem(VIEW_KEY, digital ? "digital" : "analog");
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
setView(savedView());

// トレイのメニューから入り切りされたとき。
listen("state-changed", (event) => {
  state = event.payload;
  render();
});

load().catch((error) => {
  statusElement.textContent = String(error);
});
