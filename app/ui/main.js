// イコライザーの画面。ツマミを回すたびに、Rust 側の set_state で設定ファイルを書く。
// APO は設定ファイルの変更を見て、すぐ音に反映する。
"use strict";

const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

const powerButton = document.getElementById("power");
const powerLabel = document.getElementById("power-label");
const noticeElement = document.getElementById("notice");
const noticeText = document.getElementById("notice-text");
const attachButton = document.getElementById("attach");
const statusElement = document.getElementById("status");

// ツマミは、まんなか（真上）から左右に 135 度ずつ回る。
const SWEEP_DEGREES = 270;
// 端から端まで回すのに要る、マウスの縦の移動量（px）。
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

// ツマミを 1 つ用意する。
//   key: state のどの値か / min, max, step: 範囲と刻み / reset: ダブルクリックで戻る値
//   detent: この値の近くで止まりやすくする（まんなかの位置など）。無ければ null
function setupKnob({ key, min, max, step, reset, detent, format }) {
  const element = document.getElementById(key);
  const pointer = element.querySelector(".pointer");
  const valueElement = document.getElementById(key + "-value");

  const snap = (value) => {
    let snapped = Math.round(value / step) * step;
    if (detent !== null && Math.abs(snapped - detent) <= step) snapped = detent;
    return Math.min(max, Math.max(min, snapped));
  };

  const render = () => {
    const value = state[key];
    const angle = ((value - min) / (max - min) - 0.5) * SWEEP_DEGREES;
    pointer.setAttribute("transform", `rotate(${angle})`);
    valueElement.textContent = format(value);
    element.setAttribute("aria-valuemin", String(min));
    element.setAttribute("aria-valuemax", String(max));
    element.setAttribute("aria-valuenow", String(value));
    element.setAttribute("aria-valuetext", format(value));
  };

  const change = (value) => {
    const next = snap(value);
    if (next === state[key]) return;
    state[key] = next;
    render();
    scheduleSave();
  };

  // 上下にドラッグして回す。止まりやすい位置を通り抜けられるよう、丸める前の値を覚えておく。
  let dragStartY = 0;
  let dragStartValue = 0;
  element.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    element.setPointerCapture(event.pointerId);
    element.classList.add("dragging");
    dragStartY = event.clientY;
    dragStartValue = state[key];
  });
  element.addEventListener("pointermove", (event) => {
    if (!element.hasPointerCapture(event.pointerId)) return;
    const fine = event.shiftKey ? 0.25 : 1; // Shift を押している間は細かく動かす
    const delta = ((dragStartY - event.clientY) / DRAG_PIXELS) * (max - min) * fine;
    change(dragStartValue + delta);
  });
  const endDrag = (event) => {
    if (element.hasPointerCapture(event.pointerId)) element.releasePointerCapture(event.pointerId);
    element.classList.remove("dragging");
  };
  element.addEventListener("pointerup", endDrag);
  element.addEventListener("pointercancel", endDrag);

  // ホイールで 1 刻みずつ。
  element.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      change(state[key] + (event.deltaY < 0 ? step : -step));
    },
    { passive: false }
  );

  // ダブルクリックで元の位置へ。
  element.addEventListener("dblclick", () => change(reset));

  // キーボード。
  element.addEventListener("keydown", (event) => {
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

  knobs[key] = { render };
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
    setupKnob({ key, min: -tone, max: tone, step: 0.5, reset: 0, detent: 0, format: formatDb });
  }
  setupKnob({
    key: "volume",
    min: 0,
    max: payload.maxVolumePercent,
    step: 1,
    reset: 100,
    detent: 100,
    format: formatPercent,
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

// トレイのメニューから入り切りされたとき。
listen("state-changed", (event) => {
  state = event.payload;
  render();
});

load().catch((error) => {
  statusElement.textContent = String(error);
});
