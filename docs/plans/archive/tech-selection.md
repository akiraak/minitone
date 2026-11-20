# 技術選定

## 目的・背景

sx360（Windows 11）で鳴る音の全部にイコライザーをかけるアプリを作る。
音の処理をどこで行うか、画面を何で作るかがまだ決まっていないので、調べて決める。

## 対応方針

調査は読むだけの操作で進める。Windows に何かを入れる・設定を変える操作は、この作業の中では行わない。

### Phase 1: sx360 の現状を調べる

- Windows のバージョン、出力デバイスの一覧、デバイスに既に付いている音の効果（APO）
- 音の経路に入るソフトが既に入っているか（Equalizer APO、仮想オーディオデバイスなど）
- Windows 側と WSL 側に入っている開発用のツールチェーン

### Phase 2: 音の処理方式を比べる

候補ごとに、要る権限・署名・再起動、元に戻す手順、遅延、作る量を比べる。

- Equalizer APO を使い、設定ファイルと画面だけを作る
- 自前の APO を作る
- 仮想オーディオデバイスを通し、アプリの中で処理して実デバイスへ出す
- そのほか（調査で見つかったもの）

### Phase 3: 画面のフレームワークと言語を比べる

- 候補: Tauri（Rust）、WPF / WinUI 3（C#）、Electron など
- WSL からビルドできるか、Windows に入れる必要があるもの、常駐（タスクトレイ）のしかた、配布のしかた

### Phase 4: 決めて書く（2026-11-19 完了）

- 比較結果をユーザーに示し、方式を決めてもらう
- 決めた内容を `CLAUDE.md` の「プロジェクトの目的」「開発環境」に書く

## 影響範囲

- このプランと `CLAUDE.md`、`TODO.md` だけ。コードはまだ無い
- sx360 の Windows 側には変更を加えない

## テスト方針

- 調査なのでテストは無い。比較に書く事実は、sx360 での実測か、一次情報（公式ドキュメント、プロジェクトのリポジトリ）で確かめる

## 調査結果

調査日: 2026-11-19

### Phase 1: sx360 の現状

- Windows 11 Home 25H2（build 26200）、x64
- 出力デバイス: 既定は Bluetooth のヘッドホン。ほかに本体のスピーカー（Realtek）、ディスプレイの音声、Bluetooth の機器が多数（接続していないものを含む）
- 本体のスピーカーと Bluetooth のヘッドホンには、ドライバ由来の APO が既に付いている
- Voicemeeter（標準版 1.1.2.2）が入っていて動いている。仮想デバイス（Voicemeeter Input など）もある。ただし既定の出力は Voicemeeter ではないので、いまの音は Voicemeeter を通っていない
- Equalizer APO は入っていない。`DisableProtectedAudioDG` は設定されていない
- Windows 側のツールチェーン: Rust（stable-msvc、1.83.0）、`cargo tauri`、Visual Studio Community 2022 / 2026（C++ のビルドツールあり、.NET デスクトップのワークロードは無し）、.NET SDK 8.0、WebView2 ランタイム、pnpm（Node.js 本体は無し）
- WSL 側: Rust、Node.js

### Phase 2: 音の処理方式

| 方式 | 入れるもの | 権限・再起動 | 遅延 | 作る量 | 元に戻す |
| --- | --- | --- | --- | --- | --- |
| A. Equalizer APO を使い、設定ファイルと画面を作る | Equalizer APO 1.4.2 | 管理者、再起動。デバイスごとに登録 | ほぼ無い | 小 | アンインストーラ |
| B. 自前の APO を作る | 自作の DLL | 管理者、レジストリ（デバイスごと）、再起動 | ほぼ無い | 大（C++ の COM、登録、インストーラ） | 自分で作る |
| C. 仮想デバイスに出し、アプリで処理して実デバイスへ出す | 仮想デバイス（sx360 には Voicemeeter のものが既にある） | 既定の出力を仮想デバイスへ変える | 数十 ms | 中（取り込み、再生、クロックのずれの吸収） | 既定の出力を戻す |
| D. Voicemeeter の EQ を外から操作する | Voicemeeter Banana / Potato | 既定の出力を変える | 小 | 小 | 既定の出力を戻す |

- A: Equalizer APO は GPL v2 のオープンソース。設定はテキストファイルで、書き換えるとすぐ反映される。アプリは設定ファイルを書くだけなので、Equalizer APO のコードは取り込まない。最新は 1.4.2（2025-03）で、Windows 11 の Bluetooth 機器での不具合が直されている
  - 弱点: 新しい出力デバイスを使うたびに Equalizer APO への登録が要る（管理者）。Windows の大きな更新で登録が外れることがある。25H2 での動作は公式には明言されておらず、不具合の報告が少数ある。排他モードや ASIO の音には効かない
  - 同じ作りの画面は既にある（Peace など）
- B: 音の処理を自分で書ける。ただし APO は `audiodg.exe` の中で動くので、落ちると Windows 全体の音が止まる。署名の無い APO を読ませる条件は一次情報で確かめられていない（`DisableProtectedAudioDG` を使う例があるが、DRM の再生に影響するという指摘がある）。作るものの大半は Equalizer APO の作り直しになる
- C: Windows に何も足さずに試せる。ただしアプリが動いていないと音が出ない、遅延が動画やゲームで気になりうる、音量キーが仮想デバイスに効く、という使い勝手の問題がある。仮想デバイスのドライバを自作するのは、ドライバの署名（EV 証明書と Microsoft の署名）が要るので現実的でない
- D: sx360 に入っているのは標準版で、バスのパラメトリック EQ は Banana / Potato にしか無い。EQ の中身は Voicemeeter 任せになる。外から EQ の各バンドを操作できるかは確かめられていない

**すすめる方式: A。** 遅延が無く、音の処理は実績のあるものに任せられ、作る量が少ない。まず sx360 に Equalizer APO を入れて既定のデバイス（Bluetooth）で動くかを確かめ、だめなら C を検討する。

### Phase 3: 画面のフレームワークと言語

| 候補 | sx360 に足すもの | ビルド | 常駐 |
| --- | --- | --- | --- |
| Tauri 2（Rust + Web の画面） | Rust の更新（1.83.0 は古い）。画面を素の HTML / JS で書けば Node.js は不要 | Windows 側の `cargo.exe` を WSL から呼ぶ | トレイの機能が標準である |
| WPF（C#、.NET 8） | 無し（SDK とランタイムがある） | Windows 側の `dotnet.exe` を WSL から呼ぶ | 別のライブラリか WinForms の NotifyIcon |
| Electron | Node.js | Windows 側で Node.js | 標準である |

- どれも Windows 側のツールでビルドする。WSL からのクロスビルドは、WebView2 や MSVC のリンクが絡むので採らない
- 方式 A なら、アプリの仕事は「画面で値を決める → 設定ファイルを書く」だけで、音の処理は書かない

**すすめる候補: Tauri 2。** `cargo tauri` と MSVC のビルドツール、WebView2 が既にあり、EQ のカーブの描画は Web の技術（Canvas / SVG）が向く。配布物も小さい。

### ユーザーの要件との照らし合わせ（2026-11-19）

要件: (1) 出力デバイスがスピーカーでもヘッドホンでも効く、(2) オリジナルの音に戻せる。

- (2) はどの方式でも満たせる。方式 A では設定ファイルを空にする（フィルタを書かない）だけで、すぐ元の音になる
- (1) は方式で差が出る
  - A: Equalizer APO に登録したデバイスにだけ効く。いまあるデバイスはまとめて登録できるが、新しく使い始めたデバイスは 1 回登録（管理者）が要る。アプリ側で「いまの出力デバイスが未登録」を見つけて知らせることで補う
  - C: 仮想デバイスの段で処理するので、最終の出力先が何であっても効く。代わりに遅延がある
- 接続していないデバイス（Bluetooth の機器など）を先に登録できるかは、Equalizer APO を入れたときに確かめる

### 出典

- [Equalizer APO（SourceForge）](https://sourceforge.net/projects/equalizerapo/files/)
- [Windows 11 APIs for Audio Processing Objects（Microsoft）](https://learn.microsoft.com/en-gb/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects)
- [APO Signing and Installation（OSR）](https://community.osr.com/t/apo-signing-and-installation/55812)
- [Voicemeeter Remote API](https://download.vb-audio.com/Download_CABLE/VoicemeeterRemoteAPI.pdf)

## 決定（2026-11-19）

- 音の処理: 方式 A（Equalizer APO を使い、設定ファイルと画面を作る）
- 画面: Tauri 2
- 決定の内容は `CLAUDE.md` の「作り方」に書いた

## 決定の変更（2026-11-19）

- 同じ日に、音の処理を方式 B（自前の APO を作る）へ変えた。方式 A では、使う人が Equalizer APO を別に入れる必要があるため
- 画面は Tauri 2 のまま
