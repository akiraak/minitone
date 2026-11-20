# 画面の文を英語だけにする

## 目的・背景

画面の文（知らせ、トレイのメニュー、エラー）は、Windows の表示言語が日本語なら日本語、それ以外は英語で出していた。2026-11-20 にユーザーが「英語のみに修正。日本語表示は必要ない」と決めた。

## 対応方針

- 日本語の文と、言語を切り替える仕組みを消す。
  - `app/src-tauri/src/texts.rs`（`tr`、`lang`、`MINITONE_LANG`）を消す。`main.rs` と `devices.rs` の `tr("English", "日本語")` は英語の文だけにする。
  - 画面へ渡す状態から `lang` を消す。`app/ui/main.js` の `t(英語, 日本語)` を英語の文だけにする。`index.html` の `lang="en"` はそのまま。
  - `Cargo.toml` の `Win32_Globalization`（表示言語を調べるためだけに使っていた）を消す。
- README の「日本語のときは日本語」の行と、CLAUDE.md の決まりを直す。
- コードのコメントは日本語のまま。

## 影響範囲

- 画面のアプリだけ。APO、設定ファイル、登録と解除のスクリプトは変えない。
- 日本語の Windows でも、知らせとトレイのメニューが英語になる。ツマミの名前（TREBLE、BASS、VOLUME）は元から英語。

## テスト方針

- `app/build.ps1`（`cargo test` とリリース版のビルド）が通ること。
- `app/build-installer.ps1` でインストーラをビルドする。
- ユーザーが sx360 で、トレイのメニューが英語で出ることを見る。APO が付いていないデバイスでの知らせ（"Not active on … yet." と "Enable"）の見た目は、`TODO.md` の確認の項目で見る。
