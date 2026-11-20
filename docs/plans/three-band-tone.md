# 音の調整を低音・中音・高音の 3 つにすることの検討

## 目的・背景

2026-11-20 にユーザーが「音の調整を低音・中音・高音の 3 つにすることのメリットを出して検討する」と指示した。

いまのツマミは Treble、Bass、Volume の 3 つ（2026-11-19 に決定）。

- Bass: 150 Hz から下をまとめて ±20 dB（シェルビング）。
- Treble: 5 kHz から上をまとめて ±20 dB（シェルビング）。
- 150 Hz 〜 5 kHz の約 5 オクターブは、どのツマミでも動かせない。人の声、ギター、ピアノなど、音の中身の大半はこの範囲にある。

この文書は検討だけ。コードは変えていない。

## 中音のツマミを足すメリット

1. **声を調整できる。** 動画や会議で声が聞き取りにくいときに、中音を上げると声が前に出る。いまの Treble と Bass では、声の帯域（300 Hz 〜 3 kHz）にほとんど触れない。
2. **「こもる」「きつい」を直せる。** 小さいスピーカーやノート PC の本体のスピーカーは中音に癖が出やすい。中音を少し下げると、聞き疲れが減る。
3. **低音と高音を上げずに「ドンシャリ」が作れる。** 中音を下げると、低音と高音を上げたのと近い音になる。上げないので、デジタルの上限を超えず、上げ幅を自動で抑える仕組み（`limit=1`）が働かない。
4. **なじみがある。** 低音・中音・高音の 3 つは、アンプやミキサーでよくある並び。
5. **APO は変えなくてよい。** APO は中心の周波数の周りだけを動かすフィルタ（設定ファイルの `band=`）にすでに対応していて、上げ幅を自動で抑える仕組みも `band` に効く。変えるのは画面のアプリ（`settings.rs`、`app/ui/`）だけ。

## デメリット

1. **ツマミが 4 つになるか、Volume が無くなる。**
   - Volume を残して 4 つにする: ウィンドウ（600 x 190）に入らない。幅を 730 前後に広げるか、ツマミ（116 px）を小さくする。
   - Volume を外して 3 つにする: 100% を超えて上げる機能（Windows の音量では出来ない）が無くなる。
2. **中音を上げると、音量を上げたのとほぼ同じになる。** 音のエネルギーは中音に集まっているので、中音を上げると上限を超えやすく、大きい音の場面で上げ幅が自動で減る場面が増える。上げる方向の効きは、Bass や Treble より弱く感じることがある。
3. **シンプルさが減る。** MiniTone は「ツマミ 3 つ」の小さいアプリ。2026-11-20 に「残りの機能は入れない」と決めたばかりで、それを変えることになる。
4. **直すものがある。** README の説明と画面の絵、設定ファイルの読み書きとテスト、次の版のリリース。

## 作るとしたら

- 中音は、1 kHz を中心にした山のフィルタ（`band=1000,増減,0.7`）。Q が 0.7 なら、およそ 400 Hz 〜 2.5 kHz に効き、Bass と Treble のあいだを埋める。
- 幅は、ほかの 2 つと同じ ±20 dB。
- 並びは、左から 電源、BASS、MID、TREBLE、VOLUME（低い順）。いまの TREBLE、BASS の順は変わる。
- 前の版の設定ファイルには `band` の行が無いので、中音は 0 dB として読む。

## 案

| 案 | ツマミ | ウィンドウ | 失うもの |
|---|---|---|---|
| A. いまのまま | TREBLE、BASS、VOLUME | 600 x 190 | 中音は動かせない |
| B. 中音を足す | BASS、MID、TREBLE、VOLUME | 幅を 730 前後に広げる | シンプルさが少し減る |
| C. Volume と入れ替える | BASS、MID、TREBLE | 600 x 190 | 100% を超える音量 |

## おすすめ

**B（中音を足して 4 つにする）。** いちばんの弱点は、音の中身の大半がある 150 Hz 〜 5 kHz に触れないことで、中音のツマミ 1 つで埋まる。APO は変えないので、音が止まる危険は増えない。Volume は、ほかの方法では出来ない機能（100% 超え）なので残す。

声の調整が要らず、いまの 2 つで足りているなら、A のままでよい。C はおすすめしない。

## 調べたこと（2026-11-20）

ユーザーが「実装ではなくて、まずは中音の調整を入れるべきか情報を調べて」と指示したので、ウェブで調べた。出どころの多くは、メーカーの案内、フォーラム、解説の記事で、測定にもとづくものは少ない。

### 入れる理由になること

- **声の聞き取りやすさは中音で決まる。** 聞き取りに効くのは 0.5 〜 4 kHz。台詞を聞き取りやすくする定番の調整は、1 〜 3 kHz を幅広く数 dB 上げること。いまの Bass（150 Hz から下）と Treble（5 kHz から上）は、この帯域に触れない。
- **2 つだけの製品で、中音を求める声が実際にある。** Sonos のアプリは Bass と Treble の 2 つで、利用者のフォーラムに「中音の調整が欲しい」という投稿が繰り返し立っている。台詞を強調する機能は、音楽では使えず、中音の調整の代わりにならないという意見がある。
- **ヘッドホンの癖を直せる。** 低音と高音を持ち上げた「V 字」の音作りのヘッドホンは、500 Hz 〜 1.5 kHz が引っ込み、声が奥に聞こえるという不満が多い。

### 入れない理由になること

- **家庭用のアンプの標準は、ずっと 2 つ。** トーンコントロールの元になった回路（Baxandall、1952 年）は Bass と Treble の 2 つ。耳は中音にいちばん敏感で、音量を下げたときに聞こえにくくなるのは低音と高音なので、補う必要が大きいのは両端。
- **いまの 3 つのツマミで、中音を上げ下げしたのに近いことができる。** Bass と Treble を下げて Volume を上げると、あいだの 150 Hz 〜 5 kHz が相対的に上がる。逆に、Bass と Treble を上げて Volume を下げると、中音が下がる。Sonos のフォーラムでも、同じやり方が回避策として勧められている。MiniTone は Volume が 150% まであるので、これができる。ただし、動くのは 150 Hz 〜 5 kHz 全体で、1 〜 3 kHz だけを狙うことはできない。
- **周波数が決まった中音のツマミ 1 つは、狙いが合わないことがある。** 声をはっきりさせたいなら 2 〜 3 kHz、こもりを取りたいなら 300 〜 500 Hz で、場所が違う。1 kHz に決めると、どちらにも少しずれる。ギターやベースのアンプでも、中音は周波数を選べるものが使いやすいとされる。
- **上げすぎると聞き疲れする。** 1 〜 4 kHz を上げすぎると、聞き疲れや、子音の区別がつきにくくなることがある。±20 dB の幅は中音には大きすぎる。

### 結論

**中音のツマミは、無くても困らない場面が多い。入れる決め手になるのは「声を聞き取りやすくしたい」という用途があるかどうか。**

- 音楽の好みの調整（低音を足す、高音を抑える）が主な使い道なら、いまの 3 つで足りる。中音に近いことも、いまのツマミの組み合わせでできる。上の「おすすめ」で B とした判断は、ここで改める。
- 動画や会議の声を聞き取りやすくしたいなら、入れる価値がある。その場合、中心は 1 kHz ではなく 2 kHz 前後、幅は ±20 dB より狭く（±10 dB ほど）するのがよい。

### 出どころ

- 声の帯域と調整: [Politecnico di Torino の論文](https://iris.polito.it/bitstream/11583/2906296/1/main.pdf)、[Live Design](https://www.livedesignonline.com/sound/corporate-audio-101)、[Sonos の解説](https://www.sonos.com/en/blog/what-is-eq)、[Shotcut のフォーラム](https://forum.shotcut.org/t/equalizer-15-band-audio-filter/32466)
- Sonos の利用者の要望と回避策: [中音の調整の要望](https://en.community.sonos.com/controllers-music-services-228995/need-for-user-adjustable-mid-range-eq-on-arc-sound-bars-6876482)、[3 つのバンドの要望](https://en.community.sonos.com/controllers-music-services-228995/when-will-sonos-update-the-app-with-a-3-band-eq-bass-mid-treble-6879607?postid=16671357)、[台詞が聞き取りにくいという相談](https://en.community.sonos.com/home-theater-228993/anything-to-be-done-for-beam-poor-midrange-and-poor-dialog-quality-6862760)
- 2 つのトーンコントロールの由来: [Peter Baxandall](https://www.wikipedia.org/wiki/Peter_Baxandall)、[Kevin Boone の解説](https://kevinboone.me/tone_control.html)、[Hi-Fi News](https://hifinews.com/node/134693)
- 耳の感度と音量: [Loudness compensation](https://en.wikipedia.org/wiki/Loudness_compensation)、[Equal-loudness contour](https://en.wikipedia.com/wiki/Equal-loudness_contour)
- 2 つと 3 つの比較（楽器のアンプ）: [Yamaha](https://hub.yamaha.com/guitars/bass/the-basics-of-bass-eq/)、[TalkBass](https://www.talkbass.com/threads/2-band-or-3-active-eq.1362985/)
- スピーカーとヘッドホンの癖: [Which?](https://www.which.co.uk/news/article/easy-ways-to-improve-the-sound-on-your-windows-pc-or-mac-aWsCD3E393ru)、[RTINGS の用語集](https://www.rtings.com/headphones/learn/sound-quality-glossary)、[Headphonesty](https://www.headphonesty.com/2017/01/v-shaped-sound-signature-mean/)

## 試し版（2026-11-20）

ユーザーが「先に検証をする」と指示したので、B 案の試し版を作った。まだ決定ではない。

- `settings.rs`: 状態に `mid` を足し、設定ファイルに `band=1000,増減,0.7` を書く。読むときは 1 kHz の `band` の行だけを Mid として読む。前の版のファイル（`band` が無い）は 0 dB。
- 画面: 左から 電源、BASS、MID、TREBLE、VOLUME。ウィンドウの幅を 600 から 730 に広げた。
- APO は変えていない。入れてある版の APO が、そのまま `band` の行を読む。
- README、画面の絵、`CLAUDE.md` の要件は、案が決まってから直す。

## 影響範囲（B を選んだ場合）

- `app/src-tauri/src/settings.rs`（状態、設定ファイルの読み書き、テスト）、`app/ui/`（ツマミ、ウィンドウの幅）、`tauri.conf.json`（ウィンドウの幅）、README と画面の絵、`CLAUDE.md` の要件。
- APO、登録と解除のスクリプトは変えない。

## テスト方針（B を選んだ場合）

- `settings.rs` の単体テストに、`band` の行の読み書きと、前の版のファイルの読み取りを足す。
- sx360 で、声の入った音を鳴らしながら MID を回し、効き方をユーザーが耳で確かめる。中心の周波数と Q は、そのとき決め直す。
