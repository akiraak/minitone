# CLAUDE.md

このファイルは、このリポジトリで作業する Claude Code へのガイダンスです。

## プロジェクトの目的

**sx360（Windows 11）で鳴る音の全部にかけるイコライザーアプリ。**

- 対象は Windows 全体の音（ブラウザ、ゲーム、音楽プレイヤーなど、どのアプリの音にも効く）。特定のアプリの中だけの再生機能ではない。
- sx360 の Windows 側で動く。

## 作り方（2026-11-19 に決定）

- **音の処理は自前の APO（Audio Processing Object）で行う。** APO は Windows の音のエンジン（`audiodg.exe`）に読み込まれる DLL で、出力デバイスごとに登録する。
  - 理由: 使う人がこのアプリだけを入れれば使えるようにするため。Equalizer APO を別に入れてもらう作りは採らない（ユーザーの判断）。
  - Equalizer APO は GPL v2、このリポジトリは MIT。**Equalizer APO のコードは写さない。** 仕組みの参考にとどめる。
  - APO が落ちると Windows 全体の音が止まる。APO の中の処理は小さく保ち、設定の読み込みなど失敗しうる処理は音の処理の経路の外に置く。
  - **APO は C++ で書く**（Microsoft の見本と基底クラスがあるため）。
  - **APO は署名しない。** 読み込ませるために、レジストリの `DisableProtectedAudioDG` を 1 にして Windows の署名の検査を切る（ユーザーが 2026-11-19 に了承。Equalizer APO と同じ方法）。副作用として、保護された音の経路を要求するアプリ（DRM 付きの再生など）が音を出さなくなることがある。README に書いて使う人に知らせる。
  - APO は出力デバイスごとに登録する。新しい出力デバイスは 1 回の登録（管理者の権限）が要る。
  - **APO は MFX の段に入れる。** ドライバの APO が並んでいる値の先頭に CLSID を足す。EFX の段は sx360 の Bluetooth のヘッドホンで効かなかった。MFX は RAW のモードで再生するアプリの音には効かない。
  - 音のエンジンは APO を集約（COM aggregation）して生成する。APO は集約に対応していること。
  - 登録のしかた、実装の決まりの調査結果は `docs/plans/custom-apo.md` にある。
- **画面は Tauri 2（Rust + Web の画面）。** 画面は素の HTML / CSS / JavaScript で書き、Node.js に依存しない。タスクトレイに常駐する。画面は APO に設定（各バンドの値、入り切り）を渡す。
- 比較した方式は `docs/plans/archive/tech-selection.md` にある。

## 機能の要件（決まっているもの）

- 出力デバイスがスピーカーでもヘッドホンでも効く。
- いまアクティブな出力デバイスに自動で効く。アプリでデバイスを選ばせない。
- オリジナルの音に戻せる（イコライザーを切れる）。
- 操作するのは 3 つのツマミ: **Treble、Bass、Volume**（2026-11-19 にユーザーが決定）。
  - Treble と Bass は、アンプのトーンコントロールと同じ種類のフィルタ（シェルビング）。
  - Volume は 0〜150%。100% が元の音量。
  - Treble は 5 kHz から上、Bass は 150 Hz から下を ±20 dB。周波数は仮の値。
  - Treble や Bass を上げると、音がデジタルの上限を超え、APO の後ろにある Windows のリミッターが全体を下げて、音が揺れる（2026-11-19 に確認）。これを防ぐため、APO は上げている帯域の上げ幅を、出力が上限を超えない範囲に自動で抑える（設定の `limit=1`）。全体の音量は下げない。大きい音の場面では上げ幅が減り、小さい音の場面では設定どおり上がる。経緯と試算は `docs/plans/archive/tone-wobble.md`。
- 画面は小さい横長のウィンドウ（600 x 190）。左から、電源の丸い押しボタン（直径 52 px）、TREBLE、BASS、VOLUME。黒いアルミのパネルにオレンジの指針。
- 画面のデザインは、ハードウェアのアンプの前面パネル（回すツマミ）の雰囲気にする。特定のメーカーの製品のロゴ、名前、見た目そのものは写さない。

## 経緯

- 2026-11-19 にユーザーが作ると決め、project-manager（`~/project-manager`）でリポジトリを作った。対象（Windows 全体の音）、名前（`equalizer`）、GitHub で公開することはそのときに決めた。

## 開発環境

- Claude Code は sx360（Windows 11 + WSL2）の WSL で動く。リポジトリは WSL の `~/equalizer`。
- アプリは Windows 側で動くので、動作確認は sx360 の Windows 側で行う。WSL から Windows のコマンドを呼ぶときは `cd /mnt/c` してから `powershell.exe` を使う（詳しくは `~/pc-manager/CLAUDE.md` の「Windows ホストでの操作方法」）。
- ビルドは Windows 側のツール（`cargo.exe`、`cargo tauri`、MSVC のビルドツール）で行い、WSL から `powershell.exe` 経由で呼ぶ。WSL からのクロスビルドはしない。
- sx360 の Windows 側に入れてある開発用のツール: Visual Studio の C++ ビルドツール、Windows SDK 10.0.26100（2026-11-19 に winget で入れた）、Rust。ATL は入っていない（APO は ATL を使わずに書いている）。

## ディレクトリ構成

- `apo/src/` — APO の DLL（C++）。
  - `EqApo.*` — APO の本体。`Dll.cpp` — DLL の入口と登録。
  - `Dsp.*` — 設定の読み取りとフィルタの計算。Windows に依存しない。
  - `SettingsStore.*` — 設定ファイルを見張り、変わったら読み直して APO に配る。
- `apo/test/dsp_test.cpp` — フィルタと設定の読み取りの単体テスト。WSL の g++ でもビルドできる。
- `apo/test/host_test.cpp` — DLL を Windows の音のエンジン無しで読み込み、音のエンジンが行う呼び出しをまねて確かめるテスト。
- `apo/build.ps1` — DLL とテストをビルドし、テストを走らせる。出力は `apo/build/`（コミットしない）。
- `apo/scripts/install-apo.ps1`、`uninstall-apo.ps1` — 開発用。APO を Windows に登録して出力デバイスに取り付ける／元に戻す。
- `app/src-tauri/` — 画面のアプリ（Tauri 2、Rust）。`settings.rs` が設定ファイルの書式、`main.rs` がコマンドとタスクトレイ。
- `app/ui/` — 画面（素の HTML / CSS / JavaScript）。3 つのツマミと入り切りのボタン。
- `app/build.ps1` — 画面のアプリのテストとビルド。
- `app/build-installer.ps1` — 使う人向けのインストーラ（NSIS の `setup.exe`）をビルドする。
- `app/src-tauri/installer-hooks.nsh` — インストーラのフック。インストールの後に `install-apo.ps1 -AllDevices`、アンインストールの前に `uninstall-apo.ps1` を呼ぶ。

## 設定ファイル

画面と APO は、設定ファイル `C:\ProgramData\akiraak-equalizer\config\settings.txt` でやり取りする。画面が書き、APO が読む。APO は 250 ms ごとに更新時刻を見て、変わっていれば再起動なしで反映する。

```
enabled=1
volume=1.25
preamp=0.0
limit=1
lowshelf=150,6.0,0.707
highshelf=5000,-3.0,0.707
```

- `enabled`: 0 ならオリジナルの音（素通し）。
- `volume`: 音量の倍率（0〜4。1.0 が元の音量、1.5 が 150%、0 が無音）。変えたときは約 10 ms かけてなめらかに動く。
- `preamp`: 全体の音量（dB、±24）。`volume` と掛け合わされる。
- `limit`: 1 なら、上げているフィルタの上げ幅を、出力が上限（0.98）を超えない範囲に自動で抑える。画面はいつも 1 を書く。
- `lowshelf` / `highshelf` / `band`: フィルタ。値は 周波数（Hz）、増減（dB、±24）、Q（0.1〜20）。`lowshelf` は指定の周波数から下、`highshelf` は上をまとめて上げ下げする。`band` は中心の周波数の周りだけ。合わせて 32 個まで。
- ファイルが無い、壊れているときは素通しになる。範囲外の値の行は捨てられる。
- `config` のフォルダは、一般のユーザーが書ける（登録のスクリプトが権限を付ける）。画面は管理者でなくてよい。

## ビルドとテスト

```bash
cd /mnt/c && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w ~/equalizer/apo/build.ps1)" | tr -d '\r'
```

- APO: ビルドのあとにテスト（`dsp_test.exe`、`host_test.exe`）が走る。どちらも最後に `PASS (0 failures)` が出れば成功。
- 画面のアプリは `app/build.ps1` を同じ形で呼ぶ。`cargo test` のあとにリリース版をビルドし、ユーザーの「ダウンロード」フォルダに `equalizer.exe` を置く。ビルドの出力（`target`）は `%LOCALAPPDATA%\akiraak-equalizer-dev` に置く（WSL のフォルダに置くと遅い）。
- インストーラは `app/build-installer.ps1` でビルドする（先に `apo/build.ps1`）。Tauri の CLI（バージョン 2）は、全体には入れず `%LOCALAPPDATA%\akiraak-equalizer-dev\tools` に入れてある（全体に入っているのはバージョン 1 で、ほかのプロジェクト用）。できたものは、ユーザーの「ダウンロード」フォルダの `equalizer-setup.exe`。
- **ユーザーに実行してもらうファイル（インストーラ、試すための `equalizer.exe`）は「ダウンロード」フォルダに作る**（2026-11-20、ユーザーの指示）。ビルドの途中のファイル、ツール、開発用の登録のスクリプトは `%LOCALAPPDATA%\akiraak-equalizer-dev` のまま。
- **インストーラの実行とアンインストールは、管理者の権限が要り、音の経路を変える。Claude Code は実行せず、ユーザーに実行してもらう。**
- インストーラ（NSIS）は 32 bit のプロセス。フックから PowerShell を呼ぶときは、64 bit 版を `Sysnative` 経由で明示する。そうしないと、レジストリの書き込み先が 32 bit 用の場所にずれる。
- Windows 側の Rust は 1.100.0（2026-11-19 に更新）。Tauri 2 のいまのクレートは 1.90 以上を要求する。
- ビルドは、DLL と登録・解除のスクリプトを Windows 側の `%LOCALAPPDATA%\akiraak-equalizer-dev` にもそろえて置く。管理者の PowerShell から WSL のフォルダが見えないことがあるため。
- `.ps1` は日本語を含むので、BOM 付きの UTF-8 で保存する（PowerShell 5.1 は BOM が無いと CP932 として読む）。
- **APO の登録と解除（`install-apo.ps1`、`uninstall-apo.ps1`）は管理者の権限が要り、音の経路を変える。Claude Code は実行せず、ユーザーに実行してもらう。** `-DryRun` を付けると何も変えずに行う変更だけを表示するので、これは確認なしで実行してよい。
- APO のログは `C:\ProgramData\akiraak-equalizer\equalizer_apo.log`。管理者でなくても読める。APO は LocalService の権限で動くので、登録のスクリプトがこのフォルダに書き込みの権限を付ける。

## 注意が必要な操作

次の操作は、実行前に必ずユーザーへ確認すること。

- **Windows の音の設定を変える**（既定の出力デバイス、デバイスの音の効果、音量など）。音が出なくなる、急に大きな音が出ることがある。
- **ドライバや、音の経路に入るソフト（仮想オーディオデバイス、APO など）を Windows に入れる・外す。** 管理者の権限と再起動が要ることがあり、失敗すると音が出なくなる。入れる前に、元に戻す手順を確かめる。
- レジストリの変更、管理者の権限が要る操作。
- 開発用のツールチェーン（Rust、Node.js、.NET SDK、Visual Studio の Build Tools など）を Windows に入れる。

調査（読むだけのコマンド、デバイスの一覧を見るなど）は確認なしで進めてよい。

## Git

- リモートは `git@github.com:akiraak/equalizer.git`（公開のリポジトリ）。
- コミットメッセージは日本語で、何を変えたかを 1 行目に書く。
- 秘密情報や、特定の人の環境の情報（ユーザー名、パス、デバイスの固有の ID など）はコミットしない。

## 開発管理画面

タスク・プラン・ファイルの閲覧と編集は project-manager（`http://127.0.0.1:8200`、sx360 では `~/project-manager`）で行う。下のタスク管理ルールは、その Tasks タブと Plans タブの読み方に合わせている。

## タスク管理ルール

- タスクは `TODO.md` で管理する
- **タスクの行は、親項目も含めて必ず `- [ ] 文面` の形で書く。** project-manager の Tasks タブはチェックボックス
  （`[ ]` 未着手 / `[x]` 完了 / `[~]` 進行中 / `[-]` 中止）のある行だけをタスクとして拾う。
  チェックボックスの無い行は直前のタスクのメモ扱いになる
- **`TODO.md` に書くのはタスクだけ。** メモや決定事項は、関係するタスクの下に字下げして付ける。
  プロジェクトとしての決定は `CLAUDE.md` へ、済んだ経緯は `DONE.md` へ書く
- 字下げが親子。Tasks タブはこれをツリーとして表示する
- タスク同士の関係は、そのタスクの下に字下げした **`依存:` / `派生元:` / `関連:`** の行で書く。
  相手のタスクは `「文面」` で、プランや仕様は Markdown リンクで示す
- タスクの期日は、そのタスクの下に字下げした **`期日:`** の行で書く（例: `期日: 2026-11-20`）
- タスクが完了したら `TODO.md` から該当項目を削除し、`DONE.md` に完了日（`YYYY-MM-DD`）を付けて移す
- 新しいタスクが発生したら `TODO.md` の適切なセクションに追加する
- タスクの実施前に `TODO.md` を確認し、優先度の高いものから着手する

## 作業着手ルール

作業（実装・調査いずれも）を始めるときは、コードに手を入れる前に以下を行う。

1. **プランファイルを作成する**: `docs/plans/<task-name>.md` に、目的・背景、対応方針、影響範囲、テスト方針を書く。複数の Phase / Step に分かれる場合はファイル内でも明示する
2. **`TODO.md` の該当項目にプランへのリンクを足す**（例: `[plan](docs/plans/<task-name>.md)`）。無ければ項目を追加する
3. **複数の Phase / Step がある場合は `TODO.md` に子タスクとして追加する**。完了するごとにチェックを入れる
4. **作業完了時**: 親タスクを `DONE.md` に移し、プランを `docs/plans/archive/` に移す
