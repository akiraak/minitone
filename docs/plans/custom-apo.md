# 自前の APO を作る

## 目的・背景

使う人がこのアプリだけを入れれば使えるように、音の処理を自前の APO（Audio Processing Object）で行うと決めた（[技術選定](archive/tech-selection.md)）。
APO は Windows の音のエンジン（`audiodg.exe`）に読み込まれる DLL で、作り方を誤ると Windows 全体の音が止まる。
まず仕組みと条件を調べ、何もしない（素通しの）APO を sx360 で動かしてから、イコライザーの処理を載せる。

## 対応方針

### Phase 1: 仕組みと条件を調べる（2026-11-19 完了）

結果は下の「調査結果」。

### Phase 2: 作り方を決める（2026-11-19 完了）

- 言語: C++。Windows SDK の基底クラス `CBaseAudioProcessingObject` を使う。ATL は使わない
- 署名: しない。`DisableProtectedAudioDG` = 1 で読み込ませる（ユーザーが了承）
- 入れる段: MFX。先に EFX（全部を混ぜた後。RAW の音にも効く）を試したが、sx360 の Bluetooth のヘッドホンでは効かなかった（Phase 3 の結果）
- ドライバの APO との共存: sx360 のデバイスは、段ごとに複数の APO を並べる形（`{d04e05a6-...},13` / `,14` / `,15` の複数行の値）を使っている。その並びの先頭に自分の CLSID を足すだけで済み、元の APO を自分の中から呼ぶ仕組みは要らない。並べる形になっていないデバイスで、段が既に埋まっている場合は未対応（スクリプトは何もしない）
- 設定の渡し方: 試作では `HKLM\SOFTWARE\akiraak\equalizer` のレジストリの値を、APO が初期化のときに読む。イコライザーの値を動かしながら反映する方法は Phase 4 で決める
- sx360 に入れたもの: Windows SDK 10.0.26100（winget）

以下は決める前に挙げていた項目。

- APO を書く言語（C++ か Rust か）
- どの段に入れるか（SFX / MFX / EFX）と、ドライバが既に付けている APO の扱い
- 画面から APO へ設定を渡す方法
- 署名の検査を切る設定（`DisableProtectedAudioDG`）を受け入れるか
- sx360 に入れる開発用のツール（Windows SDK など）

### Phase 3: 素通しの APO を sx360 で動かす

進み具合（2026-11-19）:

- 済み: 素通しの APO（`apo/src/`）、音のエンジン無しのテスト（`apo/test/host_test.cpp`、全部通る）、登録と解除のスクリプト（`apo/scripts/`、`-DryRun` で sx360 に対する変更の内容を確かめた）
- 残り: ユーザーが管理者の PowerShell で登録し、下の項目を確かめる

結果（2026-11-19、既定の出力 = Bluetooth のヘッドホン）:

- **EFX の段では効かなかった。** 値（`,15`）を足して音のサービスを再起動しても、音量は変わらなかった。このデバイスには、もともと EFX の段の値が無い。APO が読み込まれたかどうかは、当時のログが読めない場所にあったので分かっていない
- **MFX の段では効いた。** ドライバの APO が並んでいる値（`,14`）の先頭に足すと、音量が 25% になった。ログで、`audiodg.exe` が APO を生成・初期化し、2 ch / 44.1 kHz / 32 bit 浮動小数点でロックしたことを確かめた
- 音のエンジンは APO を集約（aggregation）して生成する（`CreateInstance: aggregated=1`）。集約に対応していない APO は生成に失敗するはず
- 解除のスクリプトで、EFX の段に足した値が消えることを確かめた
- 決定: 入れる段は MFX を既定にする。MFX は既定のモードの音に効き、RAW のモードで再生するアプリの音には効かない

確かめる順番:

1. `install-apo.ps1 -TestGainPercent 25` で既定の出力デバイス（Bluetooth のヘッドホン）にだけ取り付ける。音が小さくなれば、APO が音の経路に入っている
2. `install-apo.ps1` をもう一度（音量の指定なし）実行し、元の音量に戻ることを確かめる（素通し）
3. 本体のスピーカーなど、ほかのデバイスでも確かめる（`-AllDevices`）
4. DRM 付きの再生（配信サービスなど）で音が出るかを確かめる
5. `uninstall-apo.ps1` で解除し、レジストリが入れる前の控えと同じに戻ることを確かめる

以下は当初の方針。

- 音を変えない最小の APO を作る
- 登録と解除のスクリプトを作る。解除で元に戻ることを、控えたレジストリ（`~/equalizer-backup/`）と比べて確かめる
- 登録は管理者の権限とレジストリの変更が要るので、ユーザーが実行する
- 確かめること: 本体のスピーカーと Bluetooth のヘッドホンで音が出るか、APO が読み込まれているか、解除で元に戻るか

### Phase 4: イコライザーの処理と画面

- バンドごとのフィルタ（双二次フィルタ）、入り切り、設定の受け渡し
- Tauri 2 の画面とタスクトレイ
- この Phase は、欲しい機能が決まってから別のプランに分ける

## 影響範囲

- sx360 の Windows 側: レジストリ（APO の登録、出力デバイスごとの `FxProperties`、`DisableProtectedAudioDG`）、開発用のツール。どれもユーザーに確認してから変える
- リポジトリ: APO と画面のコード一式（これから作る）

## テスト方針

- フィルタの計算は、APO から切り離した単体テストで確かめる（Windows の音のエンジン無しで動く形にする）
- APO としての動作は sx360 で、ユーザーの耳と、レジストリやログの読み取りで確かめる
- 登録と解除は、入れる前の控えとの差分で確かめる

## 調査結果（2026-11-19）

出典は末尾。Microsoft の文書はドライバに同梱する APO を前提に書かれていて、アプリのインストーラから入れる場合は書かれていない。その部分は Equalizer APO の開発者向け文書と、APO についての覚え書き（dechamps/apo）に拠っている。

### 読み込みの条件（署名）

- APO は署名されている必要がある。署名の無い APO を読ませるには、レジストリの `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio` に `DisableProtectedAudioDG`（DWORD）= 1 を置いて、署名の検査を切る。Equalizer APO もこの方法を使っている
- **副作用: 保護された音の経路を要求するアプリ（DRM 付きの再生など）が、挙動を変えたり音を出さなくなったりすることがある**（Equalizer APO の文書の記述。どのアプリが該当するかは書かれていない）
- 検査を通る署名を個人が得る方法は、調べた範囲では見つかっていない。Microsoft の文書は「保護された内容を再生中で、APO が正しく署名されていないと読み込みに失敗する」とだけ書いている
- sx360 にはいま `DisableProtectedAudioDG` が無い

### 登録

1. COM のクラスとして登録する: `HKLM\SOFTWARE\Classes\CLSID\{CLSID}`（`InprocServer32` に DLL のパス、`ThreadingModel`）
2. 音のエンジンに APO として登録する: `HKLM\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\{CLSID}`
3. 出力デバイスに付ける: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{デバイスの GUID}\FxProperties` の値に CLSID を書く

| 段 | 値の名前 | 位置 |
| --- | --- | --- |
| SFX | `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5` | アプリごとの音。混ぜる前 |
| MFX | `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6` | 同じモードの音を混ぜた後 |
| EFX | `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7` | 全部を混ぜた後。RAW の音にも効く |

- 1 つのデバイスの各段に入れられる APO は 1 つ。sx360 の本体のスピーカーと Bluetooth のヘッドホンには、どの段にもドライバの APO が既に入っている
  - Equalizer APO は、元の APO の CLSID を控え、自分の中から元の APO を呼ぶことでドライバの処理を残している。自前の APO でも同じ仕組みが要る
- `MMDevices` のキーは管理者でも値の書き換えしかできず、キーの作成や削除はできない（sx360 での所有者は SYSTEM）。`FxProperties` が既にあるデバイスは値の追加で済む
- デバイスの GUID はドライバを入れ直すと変わる。登録のやり直しが要る
- 反映には音のサービス（`audiosrv`）の再起動が要る。再起動なしで済むかは試して確かめる
- 新しい出力デバイスには登録が要る点は、Equalizer APO と同じ

### 実装

- 必須のインターフェース: `IAudioProcessingObject`、`IAudioProcessingObjectConfiguration`、`IAudioProcessingObjectRT`、`IAudioSystemEffects`
- C++ には基底クラス `CBaseAudioProcessingObject` があり、自分で書くのは 3 つのメソッドで済む。Microsoft の見本は SYSVAD の SwapAPO
- 音の処理（`APOProcess`）の中では、待つ処理、メモリの確保、ページアウトされうるメモリの使用をしてはいけない
- APO は LocalService 相当の権限で動き、ユーザーのフォルダは読めない。設定は ProgramData など、サービスから読める場所に置く
- DLL に埋め込みのマニフェストを入れると、保護された環境で読み込みに失敗する
- 読み込みや初期化の失敗が 10 回続くと、Windows はそのデバイスの音の効果を全部切る（`PKEY_AudioEndpoint_Disable_SysFx` = 1）。`APOProcess` の中で落ちたときの挙動は文書に無い

### sx360 のビルド環境

- MSVC のビルドツールはある（Visual Studio Community 2022 / 2026）
- **Windows SDK のヘッダが見つからない**（`Windows Kits\10\Include` が無い）。APO 用のヘッダ（`audioenginebaseapo.h` など）も無い。C++ で書くなら Windows SDK を入れる必要がある。基底クラスに WDK が要るかは未確認

### 出典

- [Audio Processing Object Architecture（Microsoft）](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture)
- [Implementing Audio Processing Objects（Microsoft）](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
- [Windows 11 APIs for Audio Processing Objects（Microsoft）](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects)
- [Equalizer APO Developer documentation](https://sourceforge.net/p/equalizerapo/wiki/Developer%20documentation/)
- [dechamps/apo（APO についての覚え書き）](https://github.com/dechamps/apo)
