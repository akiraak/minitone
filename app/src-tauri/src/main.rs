// リリース版では、コンソールのウィンドウを出さない。
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod settings;

use std::fs;
use std::path::PathBuf;

use serde::Serialize;
use settings::EqState;
use tauri::menu::{Menu, MenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Emitter, Manager, WindowEvent};

/// 画面へ渡す、いまの状態。
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct StatePayload {
    state: EqState,
    /// APO が入っているか（設定のフォルダがあるか）。
    apo_installed: bool,
    settings_path: String,
    max_tone_db: f64,
    max_volume_percent: f64,
}

/// 設定のフォルダ。APO の登録のときに作られ、一般のユーザーが書ける。
fn config_dir() -> PathBuf {
    let program_data = std::env::var_os("ProgramData").unwrap_or_else(|| "C:\\ProgramData".into());
    PathBuf::from(program_data).join("akiraak-equalizer").join("config")
}

fn settings_path() -> PathBuf {
    config_dir().join("settings.txt")
}

fn read_state() -> EqState {
    match fs::read_to_string(settings_path()) {
        Ok(text) => EqState::from_file_text(&text),
        Err(_) => EqState::flat(),
    }
}

/// 設定ファイルを書く。APO が書きかけを読まないよう、別のファイルに書いてから置き換える。
fn write_state(state: &EqState) -> Result<(), String> {
    let dir = config_dir();
    if !dir.is_dir() {
        return Err("APO が入っていません（設定のフォルダがありません）。".into());
    }
    let path = settings_path();
    let temporary = dir.join("settings.txt.tmp");
    fs::write(&temporary, state.to_file_text()).map_err(|e| format!("設定を書けません: {e}"))?;
    fs::rename(&temporary, &path).map_err(|e| format!("設定を置き換えられません: {e}"))?;
    Ok(())
}

#[tauri::command]
fn get_state() -> StatePayload {
    StatePayload {
        state: read_state(),
        apo_installed: config_dir().is_dir(),
        settings_path: settings_path().to_string_lossy().into_owned(),
        max_tone_db: settings::MAX_TONE_DB,
        max_volume_percent: settings::MAX_VOLUME_PERCENT,
    }
}

#[tauri::command]
fn set_state(state: EqState) -> Result<EqState, String> {
    let state = state.sanitized();
    write_state(&state)?;
    Ok(state)
}

fn show_main_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.show();
        let _ = window.unminimize();
        let _ = window.set_focus();
    }
}

/// トレイのメニューからの入り切り。設定ファイルを書き換え、画面にも知らせる。
fn toggle_enabled(app: &AppHandle) {
    let mut state = read_state();
    state.enabled = !state.enabled;
    if write_state(&state).is_ok() {
        let _ = app.emit("state-changed", &state);
    }
}

fn main() {
    tauri::Builder::default()
        .setup(|app| {
            let open = MenuItem::with_id(app, "open", "開く", true, None::<&str>)?;
            let toggle = MenuItem::with_id(app, "toggle", "イコライザーの入り切り", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "終了", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&open, &toggle, &quit])?;

            let mut tray = TrayIconBuilder::with_id("main")
                .tooltip("Equalizer")
                .menu(&menu)
                .show_menu_on_left_click(false)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "open" => show_main_window(app),
                    "toggle" => toggle_enabled(app),
                    "quit" => app.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    // 左クリックでウィンドウを出す。右クリックはメニュー。
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = event
                    {
                        show_main_window(tray.app_handle());
                    }
                });
            if let Some(icon) = app.default_window_icon() {
                tray = tray.icon(icon.clone());
            }
            tray.build(app)?;
            Ok(())
        })
        .on_window_event(|window, event| {
            // 閉じるボタンでは終了せず、トレイに残る。終了はトレイのメニューから。
            if let WindowEvent::CloseRequested { api, .. } = event {
                let _ = window.hide();
                api.prevent_close();
            }
        })
        .invoke_handler(tauri::generate_handler![get_state, set_state])
        .run(tauri::generate_context!())
        .expect("アプリを起動できません");
}
