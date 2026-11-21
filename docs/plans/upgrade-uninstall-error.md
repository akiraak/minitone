# 上書きインストールで「Error launching installer」が出るのを直す

## 目的・背景

2026-11-20、v0.2.0 の下書きのインストーラを、0.1.0 が入っている sx360 で実行したところ、「Error launching installer」と出て進まなかった（ユーザーの報告）。v0.2.0 は公開していない。

### 原因（調べて確かめたこと）

- エラーを出しているのは、新しいインストーラから呼ばれた、前の版のアンインストーラ（`C:\Program Files\MiniTone\uninstall.exe`）。新しいインストーラの子プロセスとして動いていた。
- Tauri の NSIS のインストーラは、前の版が入っていると「アンインストールしてから入れる」（既定）か「アンインストールしない」かを選ばせる。前者では、前の版のアンインストーラを `uninstall.exe _?=<インストール先>` で呼ぶ。インストール先は、レジストリの `HKLM\Software\akiraak\MiniTone` の既定の値から読む。
- この値が無かった（sx360 で `reg query` して確かめた。キーごと無い）。`_?=` が空になり、NSIS のアンインストーラは「Error launching installer」で止まる。
- 消していたのは `install-apo.ps1`。「前の版が使っていた、動作確認用の音量の値を消す」ために `HKLM\SOFTWARE\akiraak\minitone` をキーごと消していた。レジストリは大文字と小文字を区別しないので、インストーラのキーと同じもの。内部の名前を `equalizer` から `minitone` に変えたときに、重なった。インストールのたびに、インストーラが書いた直後に消える。
- これまでの上書きインストールは同じ版（0.1.0）同士で、アンインストーラを呼ばない道だったので、出なかった。

## 対応方針

1. `install-apo.ps1`、`uninstall-apo.ps1`: `HKLM\SOFTWARE\akiraak\minitone` を消すのをやめる（この値を使っていた版は、公開していない）。
2. `installer-hooks.nsh`: 0.1.0 が入っている PC では値がもう無いので、インストーラが画面を出す前に、アンインストールの情報（`Uninstall\MiniTone` の `InstallLocation`）からインストール先を書き戻す（`MUI_CUSTOMFUNCTION_GUIINIT`）。
3. 版を 0.2.1 にして出し直す（0.2.0 の下書きは消す）。ユーザーの判断待ち。

## 結果（2026-11-20）

- ユーザーが、0.1.0 が入っている sx360 で直したインストーラ（`MiniTone-setup-fix.exe`）を実行し、「インストールできて問題なかった」と確認した。
- インストールのあと、`HKLM\SOFTWARE\akiraak\MiniTone` の既定の値（`C:\Program Files\MiniTone`）が残っていること、表示の版が 0.2.0 になったこと、APO が設定を読んでいることを、こちらで見た。
- 版を 0.2.1 にして出し直す（v0.2.0 のタグは動かさない。0.2.0 の下書きは公開しない）。

## 影響範囲

- `apo/scripts/install-apo.ps1`、`uninstall-apo.ps1`、`app/src-tauri/installer-hooks.nsh`。APO の DLL と画面のアプリは変えない。
- 0.1.0 を自分でビルドして入れた人、Releases の 0.1.0 を入れた人の両方に効く。

## テスト方針

- 手元で `apo\build.ps1`、`app\build-installer.ps1` が通ること（2026-11-20、通った。直したインストーラは「ダウンロード」フォルダの `MiniTone-setup-fix.exe`）。
- ユーザーが、0.1.0 が入っている sx360 で直したインストーラを実行し、既定の「アンインストールしてから入れる」で最後まで進むこと、音とツマミが効くことを確かめる。
- そのあと、もう一度同じインストーラを実行して、上書きでも通ることを確かめる（値が残っていること）。
