//! 画面に出す文の言語。Windows の表示言語が日本語なら日本語、それ以外は英語。

use std::sync::OnceLock;

use windows::Win32::Globalization::GetUserDefaultUILanguage;

/// Windows の言語の番号のうち、下の 10 ビットが言語（上は地域）。日本語は 0x11。
const PRIMARY_LANGUAGE_MASK: u16 = 0x3ff;
const LANG_JAPANESE: u16 = 0x11;

fn is_japanese_langid(langid: u16) -> bool {
    langid & PRIMARY_LANGUAGE_MASK == LANG_JAPANESE
}

/// 表示言語が日本語か。起動中は変わらないので、1 回だけ調べる。
pub fn is_japanese() -> bool {
    static JAPANESE: OnceLock<bool> = OnceLock::new();
    *JAPANESE.get_or_init(|| {
        // 確認用: 環境変数 MINITONE_LANG（"ja" か "en"）があれば、そちらを使う。
        match std::env::var("MINITONE_LANG").as_deref() {
            Ok("ja") => true,
            Ok("en") => false,
            _ => is_japanese_langid(unsafe { GetUserDefaultUILanguage() }),
        }
    })
}

/// 画面（JavaScript）へ渡す言語の名前。
pub fn lang() -> &'static str {
    if is_japanese() {
        "ja"
    } else {
        "en"
    }
}

/// 英語と日本語の文のうち、表示言語に合うほうを返す。
pub fn tr(en: &'static str, ja: &'static str) -> &'static str {
    if is_japanese() {
        ja
    } else {
        en
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn japanese_is_detected_from_the_primary_language() {
        assert!(is_japanese_langid(0x0411)); // ja-JP
        assert!(!is_japanese_langid(0x0409)); // en-US
        assert!(!is_japanese_langid(0x0804)); // zh-CN
    }
}
