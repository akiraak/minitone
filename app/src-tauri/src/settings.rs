//! 設定ファイルの書式。APO（apo/src/Dsp.cpp）が読む形に合わせる。
//!
//! ```text
//! enabled=1
//! volume=1.250
//! preamp=0.0
//! limit=1
//! lowshelf=150,6.0,0.707
//! band=2000,2.0,0.7
//! highshelf=5000,-3.0,0.707
//! ```

use serde::{Deserialize, Serialize};

/// Bass のツマミが効き始める周波数（Hz）。これより下をまとめて上げ下げする。
pub const BASS_HZ: f64 = 150.0;
/// Mid のツマミの中心の周波数（Hz）。この周りだけを上げ下げする。
pub const MID_HZ: f64 = 2000.0;
/// Mid のフィルタの Q。0.7 なら、およそ 1 〜 4 kHz（声の聞き取りに効く帯域）に効く。
pub const MID_Q: f64 = 0.7;
/// Treble のツマミが効き始める周波数（Hz）。これより上をまとめて上げ下げする。
pub const TREBLE_HZ: f64 = 5000.0;
/// シェルビングのフィルタの Q。山や谷のできない、なだらかな形。
pub const SHELF_Q: f64 = 0.707;
/// Treble、Mid、Bass の幅（dB）。APO 側の上限は ±24。
pub const MAX_TONE_DB: f64 = 20.0;
/// Treble や Bass を上げたときに、全体を下げる量の上限（dB）。
/// 0 なら全体は下げず、上げ幅の調整を APO の `limit=1` だけに任せる。
/// 大きくすると、大きい音の場面での上げ幅が増える代わりに、全体が小さくなる。
pub const STATIC_HEADROOM_CAP_DB: f64 = 0.0;
/// Volume の上限（%）。100 が元の音量。
pub const MAX_VOLUME_PERCENT: f64 = 150.0;

/// 画面が持つ設定。
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct EqState {
    /// false ならオリジナルの音。
    pub enabled: bool,
    /// 高音の増減（dB）。
    pub treble: f64,
    /// 中音の増減（dB）。前の版の画面が渡す状態には無いので、無ければ 0。
    #[serde(default)]
    pub mid: f64,
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
            mid: 0.0,
            bass: 0.0,
            volume: 100.0,
        }
    }

    /// 画面から渡された値を、書いてよい形に直す。
    pub fn sanitized(&self) -> Self {
        EqState {
            enabled: self.enabled,
            treble: clamp_tone(self.treble),
            mid: clamp_tone(self.mid),
            bass: clamp_tone(self.bass),
            volume: clamp_volume(self.volume),
        }
    }

    /// Treble や Bass を上げたときに、全体を下げる量（dB、0 以下）。
    ///
    /// 上げたままにすると、音がデジタルの上限を超え、Windows のリミッターが全体の音量を
    /// 自動で下げて、音が揺れる（2026-11-19 に確認）。これを防ぐ仕組みは 2 つある。
    ///
    /// - APO の `limit=1`: 上げた帯域の音が大きい瞬間だけ、その帯域の上げ幅を減らす。
    ///   全体の音量は下がらない。こちらが主役。
    /// - ここで決める量: 先に全体を下げて余裕を作る。大きい音の場面でも上げ幅が残るが、
    ///   全体が小さくなる。上げた量と STATIC_HEADROOM_CAP_DB の小さいほうだけ下げる。
    pub fn headroom_db(&self) -> f64 {
        let state = self.sanitized();
        let boost = state.treble.max(state.mid).max(state.bass).max(0.0).min(STATIC_HEADROOM_CAP_DB);
        if boost > 0.0 {
            -boost
        } else {
            0.0
        }
    }

    /// 設定ファイルの中身にする。
    pub fn to_file_text(&self) -> String {
        let state = self.sanitized();
        format!(
            "enabled={}\nvolume={:.3}\npreamp={:.1}\nlimit=1\nlowshelf={},{:.1},{}\nband={},{:.1},{}\nhighshelf={},{:.1},{}\n",
            if state.enabled { 1 } else { 0 },
            state.volume / 100.0,
            state.headroom_db(),
            BASS_HZ,
            state.bass,
            SHELF_Q,
            MID_HZ,
            state.mid,
            MID_Q,
            TREBLE_HZ,
            state.treble,
            SHELF_Q,
        )
    }

    /// 設定ファイルの中身から読む。読めない行、画面に無い項目の行は捨てる。
    /// `band` は、Mid の周波数の行だけを読む（前の版のファイルには無いので、無ければ 0 dB）。
    /// `preamp` は Treble と Bass から決まる値なので、読まない。
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
                "lowshelf" | "highshelf" | "band" => {
                    // 周波数,増減,Q のうち、増減だけを読む。
                    let parts: Vec<&str> = value.split(',').collect();
                    if parts.len() != 3 {
                        continue;
                    }
                    let Ok(gain) = parts[1].trim().parse::<f64>() else {
                        continue;
                    };
                    match key {
                        "lowshelf" => state.bass = clamp_tone(gain),
                        "highshelf" => state.treble = clamp_tone(gain),
                        _ => {
                            // Mid のツマミが書いた行だけ。ほかの周波数の band は、画面に無い項目。
                            if parts[0].trim().parse::<f64>().ok() == Some(MID_HZ) {
                                state.mid = clamp_tone(gain);
                            }
                        }
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
        assert_eq!((state.treble, state.mid, state.bass, state.volume), (0.0, 0.0, 0.0, 100.0));
    }

    #[test]
    fn writes_the_format_the_apo_reads() {
        let state = EqState {
            enabled: true,
            treble: -3.0,
            mid: 2.0,
            bass: 6.0,
            volume: 125.0,
        };
        assert_eq!(
            state.to_file_text(),
            "enabled=1\nvolume=1.250\npreamp=0.0\nlimit=1\nlowshelf=150,6.0,0.707\nband=2000,2.0,0.7\nhighshelf=5000,-3.0,0.707\n"
        );
    }

    #[test]
    fn static_headroom_never_exceeds_its_cap() {
        let state = |treble: f64, bass: f64| EqState { enabled: true, treble, mid: 0.0, bass, volume: 100.0 };
        // 上げていなければ下げない。
        assert_eq!(state(0.0, 0.0).headroom_db(), 0.0);
        assert_eq!(state(-8.0, -20.0).headroom_db(), 0.0);
        // 上げたときは、上げた量と上限の小さいほうだけ下げる。
        for (treble, bass) in [(6.0, 0.0), (-3.0, 12.5), (20.0, 9.0)] {
            let expected = -f64::max(treble, bass).min(STATIC_HEADROOM_CAP_DB);
            assert_eq!(state(treble, bass).headroom_db(), if expected == 0.0 { 0.0 } else { expected });
        }
        // "-0.0" とは書かない。
        assert!(!state(0.0, -5.0).to_file_text().contains("preamp=-0.0"));
        assert!(!state(20.0, 20.0).to_file_text().contains("preamp=-0.0"));
        // 上げ幅を抑える指定を、いつも書く。
        assert!(state(20.0, 20.0).to_file_text().contains("\nlimit=1\n"));
    }

    #[test]
    fn round_trips() {
        for state in [
            EqState { enabled: true, treble: 20.0, mid: -7.5, bass: -20.0, volume: 150.0 },
            EqState { enabled: false, treble: -0.5, mid: 20.0, bass: 2.5, volume: 0.0 },
            EqState { enabled: true, treble: 0.0, mid: 0.0, bass: 0.0, volume: 33.0 },
        ] {
            assert_eq!(EqState::from_file_text(&state.to_file_text()), state);
        }
    }

    #[test]
    fn sanitizes_out_of_range_values() {
        let state = EqState {
            enabled: true,
            treble: 99.0,
            mid: -99.0,
            bass: f64::NAN,
            volume: 400.0,
        }
        .sanitized();
        assert_eq!((state.treble, state.mid, state.bass, state.volume), (20.0, -20.0, 0.0, 150.0));

        let state = EqState { enabled: true, treble: -99.0, mid: 0.04, bass: 3.14159, volume: -5.0 }.sanitized();
        assert_eq!((state.treble, state.mid, state.bass, state.volume), (-20.0, 0.0, 3.1, 0.0));
    }

    #[test]
    fn reads_files_written_by_hand() {
        let text = "\u{feff}# comment\r\nenabled=1\r\nvolume = 0.5\r\nlowshelf=80,23,0.9\r\nband=2000,5,1\r\nband=3000,9,1\r\ngarbage\r\nhighshelf=1,2\r\n";
        let state = EqState::from_file_text(text);
        assert!(state.enabled);
        assert_eq!(state.volume, 50.0);
        // 幅を超える値は幅に収める。
        assert_eq!(state.bass, 20.0);
        // Mid は、その周波数の band の行だけを読む。
        assert_eq!(state.mid, 5.0);
        // 壊れた行と、画面に無い項目（ほかの周波数の band）は捨てる。
        assert_eq!(state.treble, 0.0);
        // 前の版のファイル（band の行が無い）は、Mid を 0 として読む。
        assert_eq!(EqState::from_file_text("enabled=1\nlowshelf=150,6.0,0.707\nhighshelf=5000,-3.0,0.707\n").mid, 0.0);
    }

    #[test]
    fn empty_text_is_flat() {
        assert_eq!(EqState::from_file_text(""), EqState::flat());
    }
}
