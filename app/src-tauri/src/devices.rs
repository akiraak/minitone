//! いまの出力デバイスに APO が付いているかを調べ、付いていなければ取り付ける。
//!
//! APO は出力デバイスごとに取り付ける。インストーラは、入れた時点で Windows にあるデバイスに
//! だけ取り付けるので、あとからつないだヘッドホンなどにはツマミが効かない。

use std::path::PathBuf;

use serde::Serialize;
use windows::core::{w, HSTRING, PCWSTR};
use windows::Win32::Foundation::{CloseHandle, ERROR_CANCELLED};
use windows::Win32::Media::Audio::{eConsole, eRender, IMMDeviceEnumerator, MMDeviceEnumerator};
use windows::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, CoTaskMemFree, CLSCTX_ALL, COINIT_MULTITHREADED,
};
use windows::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject, INFINITE};
use windows::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
use windows::Win32::UI::WindowsAndMessaging::SW_HIDE;
use winreg::enums::{HKEY_LOCAL_MACHINE, KEY_READ};
use winreg::RegKey;

/// この APO の CLSID。`apo/src/EqApo.cpp` と同じ値。
const APO_CLSID: &str = "{40874C30-A23F-4E8D-B383-B40845BA5584}";
/// 出力デバイスの一覧があるレジストリのキー。
const RENDER_KEY: &str = r"SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render";
/// デバイスに付いている音の効果（APO）の CLSID が入る値の名前の先頭。
const FX_VALUE_PREFIX: &str = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},";
/// デバイスの名前（「ヘッドホン」など）と、つながっている機器の名前が入る値。
const NAME_VALUE: &str = "{a45c254e-df1c-4efd-8020-67d146a850e0},2";
const ADAPTER_VALUE: &str = "{b3f8fa53-0004-438e-9003-51a46e139bfc},6";

/// 画面へ渡す、いまの出力デバイスの状態。
#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceStatus {
    /// APO が Windows に登録されているか。false なら、インストーラでの入れ直しが要る。
    pub apo_registered: bool,
    /// いまの既定の出力デバイス。出力デバイスが 1 つも無いときは None。
    pub device_id: Option<String>,
    pub device_name: String,
    /// そのデバイスに APO が付いているか。
    pub attached: bool,
}

/// レジストリの値の中身（UTF-16 の文字列、または文字列の並び）に、この APO の CLSID があるか。
pub fn value_has_clsid(bytes: &[u8]) -> bool {
    let units: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect();
    String::from_utf16_lossy(&units)
        .to_ascii_uppercase()
        .contains(APO_CLSID)
}

/// いまの既定の出力デバイスの ID（レジストリのキーの名前になっている GUID）。
fn default_render_endpoint() -> Option<String> {
    unsafe {
        // このスレッドで COM を使えるようにする。既に済んでいれば何も起きない。
        let _ = CoInitializeEx(None, COINIT_MULTITHREADED);
        let enumerator: IMMDeviceEnumerator =
            CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL).ok()?;
        let device = enumerator.GetDefaultAudioEndpoint(eRender, eConsole).ok()?;
        let id = device.GetId().ok()?;
        let text = id.to_string().ok();
        CoTaskMemFree(Some(id.0 as *const _));
        // "{0.0.0.00000000}.{GUID}" の後ろの GUID が、レジストリのキーの名前。
        text?.rsplit('.').next().map(|guid| guid.to_string())
    }
}

fn apo_registered() -> bool {
    RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey_with_flags(format!(r"SOFTWARE\Classes\CLSID\{APO_CLSID}"), KEY_READ)
        .is_ok()
}

fn endpoint_name(endpoint: &str) -> String {
    let key = RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey_with_flags(format!(r"{RENDER_KEY}\{endpoint}\Properties"), KEY_READ);
    let Ok(key) = key else {
        return String::new();
    };
    let name: String = key.get_value(NAME_VALUE).unwrap_or_default();
    let adapter: String = key.get_value(ADAPTER_VALUE).unwrap_or_default();
    match (name.is_empty(), adapter.is_empty()) {
        (false, false) => format!("{name} ({adapter})"),
        (false, true) => name,
        _ => adapter,
    }
}

fn endpoint_attached(endpoint: &str) -> bool {
    let key = RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey_with_flags(format!(r"{RENDER_KEY}\{endpoint}\FxProperties"), KEY_READ);
    let Ok(key) = key else {
        return false;
    };
    key.enum_values().flatten().any(|(name, value)| {
        name.to_ascii_lowercase().starts_with(FX_VALUE_PREFIX) && value_has_clsid(&value.bytes)
    })
}

/// いまの出力デバイスの状態を調べる。読むだけなので、管理者でなくてよい。
pub fn current_status() -> DeviceStatus {
    let device_id = default_render_endpoint();
    let (device_name, attached) = match &device_id {
        Some(id) => (endpoint_name(id), endpoint_attached(id)),
        None => (String::new(), false),
    };
    DeviceStatus {
        apo_registered: apo_registered(),
        device_id,
        device_name,
        attached,
    }
}

/// インストール先に置いてある登録のスクリプト。インストーラで入れた版にだけある。
fn install_script() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let script = exe.parent()?.join("apo").join("install-apo.ps1");
    script.is_file().then_some(script)
}

/// 登録のスクリプトを管理者として起動し、終わるのを待つ。Windows の確認（UAC）が出る。
/// 有効な出力デバイスのうち、まだ付いていないもの全部に取り付ける。音のサービスが再起動される。
pub fn attach_all_devices() -> Result<(), String> {
    let script = install_script().ok_or_else(|| {
        "登録のスクリプトがありません。インストーラで入れた版から実行してください。".to_string()
    })?;
    let system_root = std::env::var("SystemRoot").unwrap_or_else(|_| r"C:\Windows".into());
    let powershell =
        HSTRING::from(format!(r"{system_root}\System32\WindowsPowerShell\v1.0\powershell.exe"));
    let arguments = HSTRING::from(format!(
        "-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"{}\" -AllDevices",
        script.display()
    ));

    let mut info = SHELLEXECUTEINFOW {
        cbSize: std::mem::size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS,
        lpVerb: w!("runas"),
        lpFile: PCWSTR(powershell.as_ptr()),
        lpParameters: PCWSTR(arguments.as_ptr()),
        nShow: SW_HIDE.0,
        ..Default::default()
    };

    unsafe {
        if let Err(error) = ShellExecuteExW(&mut info) {
            return Err(if error.code() == ERROR_CANCELLED.to_hresult() {
                "取り消されました。何も変えていません。".to_string()
            } else {
                format!("管理者として起動できませんでした: {error}")
            });
        }
        if info.hProcess.is_invalid() {
            return Err("登録のスクリプトを起動できませんでした。".to_string());
        }
        WaitForSingleObject(info.hProcess, INFINITE);
        let mut exit_code = 0u32;
        let result = GetExitCodeProcess(info.hProcess, &mut exit_code);
        let _ = CloseHandle(info.hProcess);
        result.map_err(|error| format!("結果を取得できませんでした: {error}"))?;
        if exit_code != 0 {
            return Err(format!("登録のスクリプトが失敗しました（終了コード {exit_code}）。"));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn utf16(text: &str) -> Vec<u8> {
        text.encode_utf16().flat_map(|unit| unit.to_le_bytes()).collect()
    }

    #[test]
    fn finds_the_clsid_in_a_single_string() {
        assert!(value_has_clsid(&utf16("{40874C30-A23F-4E8D-B383-B40845BA5584}\0")));
    }

    #[test]
    fn finds_the_clsid_in_a_list_of_strings_in_any_case() {
        let list = "{13AB3EBD-137E-4903-9D89-60BE8277FD17}\0{40874c30-a23f-4e8d-b383-b40845ba5584}\0\0";
        assert!(value_has_clsid(&utf16(list)));
    }

    #[test]
    fn ignores_other_clsids_and_empty_values() {
        assert!(!value_has_clsid(&utf16("{13AB3EBD-137E-4903-9D89-60BE8277FD17}\0\0")));
        assert!(!value_has_clsid(&[]));
        // 奇数の長さ（壊れた値）でも落ちない。
        assert!(!value_has_clsid(&[0x7b]));
    }
}
