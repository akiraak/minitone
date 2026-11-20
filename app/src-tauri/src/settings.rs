//! 設定ファイルの書式。APO（apo/src/Dsp.cpp）が読む形に合わせる。
//!
//! ```text
//! enabled=1
//! volume=1.250
//! lowshelf=150,6.0,0.707
//! highshelf=5000,-3.0,0.707
//! ```

use serde::{Deserialize, Serialize};

/// Bass のツマミが効き始める周波数（Hz）。これより下をまとめて上げ下げする。
pub const BASS_HZ: f64 = 150.0;
/// Treble のツマミが効き始める周波数（Hz）。これより上をまとめて上げ下げする。
pub const TREBLE_HZ: f64 = 5000.0;
/// シェルビングのフィルタの Q。山や谷のできない、なだらかな形。
pub const SHELF_Q: f64 = 0.707;
/// Treble と Bass の幅（dB）。APO 側の上限は ±24。
pub const MAX_TONE_DB: f64 = 20.0;
/// Volume の上限（%）。100 が元の音量。
pub const MAX_VOLUME_PERCENT: f64 = 150.0;

/// 画面が持つ設定。
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct EqState {
    /// false ならオリジナルの音。
    pub enabled: bool,
    /// 高音の増減（dB）。
    pub treble: f64,
    /// 低音の増減（dB）。
    pub bass: f64,
    /// 音量（%）。100 が元の音量。
    pub volume: f64,
}

fn clamp_tone(value: f64) -> f64 {
    if value.is_finite() {
        // 0.1 dB 刻みに丸める。
        (value.clamp(-MAX_TONE_DB, MAX_TONE_DB) * 10.0).round() / 10.0
    } else {
        0.0
    }
}

fn clamp_volume(value: f64) -> f64 {
    if value.is_finite() {
        value.clamp(0.0, MAX_VOLUME_PERCENT).round()
    } else {
        100.0
    }
}

impl EqState {
    /// ツマミが全部まんなか（音を変えない位置）で、切ってある状態。
    pub fn flat() -> Self {
        EqState {
            enabled: false,
            treble: 0.0,
            bass: 0.0,
            volume: 100.0,
        }
    }

    /// 画面から渡された値を、書いてよい形に直す。
    pub fn sanitized(&self) -> Self {
        EqState {
            enabled: self.enabled,
            treble: clamp_tone(self.treble),
            bass: clamp_tone(self.bass),
            volume: clamp_volume(self.volume),
        }
    }

    /// 設定ファイルの中身にする。
    pub fn to_file_text(&self) -> String {
        let state = self.sanitized();
        format!(
            "enabled={}\nvolume={:.3}\nlowshelf={},{:.1},{}\nhighshelf={},{:.1},{}\n",
            if state.enabled { 1 } else { 0 },
            state.volume / 100.0,
            BASS_HZ,
            state.bass,
            SHELF_Q,
            TREBLE_HZ,
            state.treble,
            SHELF_Q,
        )
    }

    /// 設定ファイルの中身から読む。読めない行、画面に無い項目の行は捨てる。
    pub fn from_file_text(text: &str) -> Self {
        let mut state = EqState::flat();
        for line in text.trim_start_matches('\u{feff}').lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let Some((key, value)) = line.split_once('=') else {
                continue;
            };
            let key = key.trim();
            match key {
                "enabled" => {
                    if let Ok(number) = value.trim().parse::<f64>() {
                        state.enabled = number != 0.0;
                    }
                }
                "volume" => {
                    if let Ok(number) = value.trim().parse::<f64>() {
                        state.volume = clamp_volume(number * 100.0);
                    }
                }
                "lowshelf" | "highshelf" => {
                    // 周波数,増減,Q のうち、増減だけを読む。
                    let parts: Vec<&str> = value.split(',').collect();
                    if parts.len() != 3 {
                        continue;
                    }
                    let Ok(gain) = parts[1].trim().parse::<f64>() else {
                        continue;
                    };
                    if key == "lowshelf" {
                        state.bass = clamp_tone(gain);
                    } else {
                        state.treble = clamp_tone(gain);
                    }
                }
                _ => {}
            }
        }
        state
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flat_changes_nothing_and_is_disabled() {
        let state = EqState::flat();
        assert!(!state.enabled);
        assert_eq!((state.treble, state.bass, state.volume), (0.0, 0.0, 100.0));
    }

    #[test]
    fn writes_the_format_the_apo_reads() {
        let state = EqState {
            enabled: true,
            treble: -3.0,
            bass: 6.0,
            volume: 125.0,
        };
        assert_eq!(
            state.to_file_text(),
            "enabled=1\nvolume=1.250\nlowshelf=150,6.0,0.707\nhighshelf=5000,-3.0,0.707\n"
        );
    }

    #[test]
    fn round_trips() {
        for state in [
            EqState { enabled: true, treble: 20.0, bass: -20.0, volume: 150.0 },
            EqState { enabled: false, treble: -0.5, bass: 2.5, volume: 0.0 },
            EqState { enabled: true, treble: 0.0, bass: 0.0, volume: 33.0 },
        ] {
            assert_eq!(EqState::from_file_text(&state.to_file_text()), state);
        }
    }

    #[test]
    fn sanitizes_out_of_range_values() {
        let state = EqState {
            enabled: true,
            treble: 99.0,
            bass: f64::NAN,
            volume: 400.0,
        }
        .sanitized();
        assert_eq!((state.treble, state.bass, state.volume), (20.0, 0.0, 150.0));

        let state = EqState { enabled: true, treble: -99.0, bass: 3.14159, volume: -5.0 }.sanitized();
        assert_eq!((state.treble, state.bass, state.volume), (-20.0, 3.1, 0.0));
    }

    #[test]
    fn reads_files_written_by_hand() {
        let text = "\u{feff}# comment\r\nenabled=1\r\nvolume = 0.5\r\nlowshelf=80,23,0.9\r\nband=1000,5,1\r\ngarbage\r\nhighshelf=1,2\r\n";
        let state = EqState::from_file_text(text);
        assert!(state.enabled);
        assert_eq!(state.volume, 50.0);
        // 幅を超える値は幅に収める。
        assert_eq!(state.bass, 20.0);
        // 壊れた行と、画面に無い項目（band）は捨てる。
        assert_eq!(state.treble, 0.0);
    }

    #[test]
    fn empty_text_is_flat() {
        assert_eq!(EqState::from_file_text(""), EqState::flat());
    }
}
