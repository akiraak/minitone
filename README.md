# equalizer

Windows で鳴る音の全部にかける、Treble・Bass・Volume のツマミ。ブラウザ、ゲーム、音楽プレイヤーなど、どのアプリの音にも効く。

A system-wide tone control for Windows 11: Treble, Bass and Volume knobs that apply to all audio. It installs its own Audio Processing Object (APO) into the Windows audio engine. Work in progress — read "入れる前に知っておくこと" before installing.

## いまの状態

開発中。作者の PC（Windows 11 25H2、x64）1 台でだけ動かしている。

- できること: Treble と Bass を ±20 dB、Volume を 0〜150% で動かす。ボタン 1 つでオリジナルの音に戻す。タスクトレイに常駐する
- まだ無いもの: 使う人向けのインストーラ。いまは開発用のスクリプトを管理者の PowerShell で動かして入れる
- 確かめていないこと: 下の「確かめていないこと」を見ること

## 入れる前に知っておくこと

このアプリは、Windows の音の経路に自前の部品（APO）を入れる。入れると次のものが変わる。

- **Windows の署名の検査を切る。** レジストリの `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio` に `DisableProtectedAudioDG` = 1 を置く。この APO には署名が無く、こうしないと Windows が読み込まないため。
  - 副作用として、保護された音の経路を要求するアプリ（DRM 付きの再生など）が、音を出さなくなることがある。
  - [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) も同じ設定を使っている。
- **出力デバイスのレジストリに値を足す。** 各デバイスの `FxProperties` に、この APO の CLSID を書く。
- **音のサービス（`audiosrv`）を再起動する。** 入れるときと外すときに、再生中の音が一度止まる。
- 管理者の権限が要る。

APO は Windows の音のエンジン（`audiodg.exe`）の中で動く。APO に不具合があると、Windows 全体の音が止まることがある。そのときは「元に戻す」の手順で外す。

## 仕組み

```
画面のアプリ（Tauri 2）  ──書く──▶  設定ファイル  ◀──読む──  APO（audiodg.exe の中）
   ツマミを回す                    settings.txt              フィルタをかける
```

- **APO**（`apo/`、C++）: 音のエンジンに読み込まれる DLL。設定ファイルを 250 ms ごとに見て、変わっていれば再起動なしで反映する。
- **画面のアプリ**（`app/`、Rust + 素の HTML / CSS / JavaScript）: ツマミの値を設定ファイルに書くだけ。音は処理しない。管理者の権限は要らない。
- **設定ファイル**: `C:\ProgramData\akiraak-equalizer\config\settings.txt`

```
enabled=1
volume=1.250
preamp=0.0
limit=1
lowshelf=150,6.0,0.707
highshelf=5000,-3.0,0.707
```

| 行 | 意味 |
| --- | --- |
| `enabled` | 0 ならオリジナルの音（素通し） |
| `volume` | 音量の倍率。1.0 が元の音量、1.5 が 150% |
| `preamp` | 全体の音量（dB）。画面は 0 を書く |
| `limit` | 1 なら、上げているフィルタの上げ幅を、上限を超えない範囲に自動で抑える |
| `lowshelf` | Bass。周波数（Hz）、増減（dB）、Q。指定の周波数から下をまとめて上げ下げする |
| `highshelf` | Treble。指定の周波数から上をまとめて上げ下げする |
| `band` | 中心の周波数の周りだけを上げ下げする。画面からは使っていないが、手で書けば効く |

ファイルが無い、または壊れているときは素通しになる。

## 要るもの

- Windows 11（x64）
- ビルドに: Visual Studio の C++ ビルドツール、Windows SDK（10.0.26100 で確認）、Rust（1.90 以上）
- 実行に: WebView2 ランタイム（Windows 11 には入っている）

## ビルド

Windows の PowerShell で実行する。

```powershell
powershell -ExecutionPolicy Bypass -File apo\build.ps1
powershell -ExecutionPolicy Bypass -File app\build.ps1
```

- `apo\build.ps1` は DLL をビルドし、テストを走らせる。
- `app\build.ps1` はテストを走らせ、画面のアプリをビルドする。
- どちらも、できたものを `%LOCALAPPDATA%\akiraak-equalizer-dev` に置く（DLL、登録と解除のスクリプト、`equalizer.exe`）。

## 入れる

管理者の PowerShell で実行する。

```powershell
cd $env:LOCALAPPDATA\akiraak-equalizer-dev
powershell -ExecutionPolicy Bypass -File .\install-apo.ps1 -AllDevices
```

- `-DryRun` を付けると、何も変えずに、行う変更だけを表示する。先にこれで確かめるとよい。
- `-AllDevices` を付けないと、いまの既定の出力デバイスにだけ取り付ける。
- 変える前のレジストリを `C:\ProgramData\akiraak-equalizer` に書き出す。

入れたら `equalizer.exe` を起動する。右上のボタンを ON にしてツマミを回すと、音が変わる。

- ツマミ: 上下にドラッグ、ホイール、矢印キー。Shift を押しながらドラッグすると細かく動く。ダブルクリックで元の位置（0 dB、100%）に戻る。
- 閉じるボタンではタスクトレイに残る。終了は、トレイのアイコンを右クリックして「終了」。

## 元に戻す

管理者の PowerShell で実行する。

```powershell
cd $env:LOCALAPPDATA\akiraak-equalizer-dev
powershell -ExecutionPolicy Bypass -File .\uninstall-apo.ps1
```

出力デバイスから APO を外し、登録を消し、`DisableProtectedAudioDG` を入れる前の状態に戻し、DLL を消す。入れる前のレジストリの控えは `C:\ProgramData\akiraak-equalizer` に残る。

すぐ音だけ元に戻したいときは、画面のボタンを OFF にするか、設定ファイルの 1 行目を `enabled=0` にする。

## 制約

- **出力デバイスごとに取り付けが要る。** 入れたあとで新しく使い始めたデバイス（新しい Bluetooth の機器など）には効かない。`install-apo.ps1 -AllDevices` をもう一度実行する。
- **ドライバを入れ直すと外れる。** デバイスの ID が変わるため。Windows の大きな更新でも外れることがある。
- **効かない音がある。** 排他モードや ASIO で再生するアプリ、RAW のモードで再生するアプリの音は、この APO を通らない。
- **大きい音の場面では、Treble や Bass の上げ幅が減る。** 上げたままだと音がデジタルの上限を超え、Windows のリミッターが働いて全体の音が揺れる。それを防ぐため、上げた帯域の上げ幅を、上限を超えない範囲に自動で抑えている。全体の音量は変わらない。小さい音の場面では、設定どおり上がる。
- **Volume を 100% より上げると、音が揺れたり割れたりすることがある。** 音量の大きい曲で、上限を超えるため。
- 設定は全部の出力デバイスで共通。スピーカーとヘッドホンで別の設定は持てない。
- ドライバが APO を古い形式（1 つの段に 1 つだけ）で登録しているデバイスで、その段が既に埋まっている場合は取り付けない。

## 確かめていないこと

- 作者の PC 以外での動作
- DRM 付きの再生（配信サービスなど）で音が出るか
- `uninstall-apo.ps1` のあと、レジストリが入れる前と完全に同じに戻るか（APO を外して登録を消すところまでは動かした）
- Treble と Bass の効き始める周波数（150 Hz、5 kHz）は仮の値

## ログ

APO のログは `C:\ProgramData\akiraak-equalizer\equalizer_apo.log`。音が変わらないときは、ここに `audiodg.exe` からの記録があるかを見る。

## License

MIT。Equalizer APO（GPL v2）のコードは含まない。
