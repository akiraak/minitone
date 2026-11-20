# アプリ名を MiniTone にする

## 目的・背景

いまのアプリ名は `equalizer`（リポジトリの名前のまま）。中身は Treble・Bass・Volume のツマミで、名前から中身が分かりにくい。
2026-11-20 にユーザーが、アプリ名を **MiniTone** にすると決めた。

## 対応方針

使う人の目に入る名前も、内部の名前も、`equalizer` を MiniTone（内部では `minitone`）に変える。
はじめは内部の名前を残す方針だったが、同じ日にユーザーが「リポジトリ名を含めて変える」と決めた。

### 変える

- ウィンドウの題、タスクトレイのツールチップ（`MiniTone`）
- Tauri の `productName`（スタートメニュー、「インストールされているアプリ」の表示、インストール先 `C:\Program Files\MiniTone`）
- 実行ファイルの名前（`MiniTone.exe`）。Cargo のパッケージ名は `minitone`、Tauri の `mainBinaryName` は `MiniTone`
- Tauri の `identifier`（`com.github.akiraak.minitone`）
- インストーラのファイル名（`MiniTone-setup.exe`）、試すための実行ファイル（`MiniTone.exe`）
- 設定とログのフォルダ（`C:\ProgramData\akiraak-minitone`）、ログのファイル名（`minitone_apo.log`）
- APO の DLL の名前（`minitone_apo.dll`）、DLL の置き場所（`C:\Program Files\akiraak-minitone`）、レジストリの `SOFTWARE\akiraak\minitone`
- 開発用のフォルダ（`%LOCALAPPDATA%\akiraak-minitone-dev`）。いまあるフォルダの名前を変える
- GitHub のリポジトリの名前（`akiraak/minitone`）と、手元のリモートの URL
- WSL のフォルダ（`~/minitone`）と project-manager の登録。Claude Code のセッションが動いている間は変えられないので、あとで行う
- README、CLAUDE.md、進行中のプラン

### 変えない

- APO の CLSID（名前を含まない）
- 済んだ記録（`DONE.md`、`docs/plans/archive/`）と、別のソフト「Equalizer APO」を指す記述
- WSL の `~/equalizer-backup/`（入れる前のレジストリの控え）

## 影響範囲

- `productName` が変わると、インストーラは前の版（`equalizer`）を別のアプリとして扱い、上書きしない。**前の版を先にアンインストールしてから、新しい版を入れる。** 前の版のアンインストーラは、前の名前の DLL とレジストリを消す
- 設定のフォルダが変わるので、前の版の設定（ツマミの位置）は引き継がれない。前の版のフォルダ `C:\ProgramData\akiraak-equalizer`（設定、ログ、控え）は、アンインストールしても残る
- 「ダウンロード」フォルダに残る古い `equalizer.exe`、`equalizer-setup.exe` は、ビルドでは消えない

## テスト方針

- `apo/build.ps1` が通る（`dsp_test`、`host_test` が `PASS`）
- `app/build.ps1`（`cargo test` とリリース版のビルド）が通り、「ダウンロード」フォルダに `MiniTone.exe` ができる
- `app/build-installer.ps1` が通り、「ダウンロード」フォルダに `MiniTone-setup.exe` ができる
- ユーザーが、前の版をアンインストールしてから新しい版を入れ、スタートメニュー、ウィンドウの題、「インストールされているアプリ」の表示が MiniTone になっていること、音が変わることを確かめる
