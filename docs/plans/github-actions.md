# GitHub Actions で、タグからインストーラをビルドして Releases に置く

## 目的・背景

いまのリリースは、sx360 で手でビルドして `gh release create` で上げている。タグを付けたら、GitHub の上でテストとビルドが走り、インストーラが Releases に置かれるようにする。作者の PC の状態に左右されないビルドになる。

## 対応方針

- `.github/workflows/release.yml` を足す。`windows-latest` のランナーで、いまのビルドのスクリプトをそのまま呼ぶ（`apo\build.ps1` → `app\build.ps1` → `app\build-installer.ps1`）。スクリプトは変えない。
  - ランナーには Visual Studio の C++ ビルドツール、Windows SDK、Rust が入っている。
  - Tauri の CLI は、スクリプトが決めた場所（`%LOCALAPPDATA%\akiraak-minitone-dev\tools`）に `cargo install` で入れ、キャッシュする。
- きっかけは 2 つ。
  - `v*` のタグのプッシュ: タグとバージョン（`tauri.conf.json`、`Cargo.toml`）が合っているかを確かめ、ビルドして、Releases に**下書き**を作る。説明には SHA-256 と、GitHub が作る変更点の一覧を入れる。
  - 手で動かす（`workflow_dispatch`）: ビルドとテストだけ。インストーラは Artifacts に置き、Releases には触らない。
- 下書きにする理由: これまでは、ユーザーがインストーラを入れて確かめてから公開していた。GitHub の上でビルドしたものは、手元で試したものとは別のファイルなので、落として確かめてから公開する。
- インストーラは署名しない（2026-11-20 の決定のまま）。

## 影響範囲

- 足すのは `.github/workflows/release.yml` だけ。アプリ、APO、ビルドのスクリプトは変えない。
- リリースの手順が変わる（`docs/plans/distribution.md` と `CLAUDE.md` に書く）。

## テスト方針

- Step 1: ワークフローを手で動かし、APO のテスト（`dsp_test.exe`、`host_test.exe`）、`cargo test`、インストーラのビルドが通ることを見る。
- Step 2: 次の版のタグをプッシュしたときに、下書きができ、インストーラと SHA-256 が付いていることを見る。タグのプッシュはリリースの作業なので、ユーザーが版を上げると決めたときに行う。
