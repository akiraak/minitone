// 設定ファイルを見張り、いまの設定を APO の全部のインスタンスに配る。プロセスに 1 つ。
#pragma once

#include "Dsp.h"

namespace eq
{

// APO のインスタンスが生きている間、見張りのスレッドを動かしておく。
// 最初の Acquire で設定を読み込んでスレッドを起こし、最後の Release で止める。
// どちらもリアルタイムのスレッドから呼ばないこと。
void StoreAcquire();
void StoreRelease();

// 設定の版の番号。設定が読み直されるたびに進む。リアルタイムのスレッドから呼べる。
unsigned StoreVersion();

// いまの設定を写す。待たない。ほかのスレッドが書き換えている最中なら false を返すので、
// 次の回にやり直す。リアルタイムのスレッドから呼べる。
bool StoreTryRead(Settings* settings, unsigned* version);

// いまの設定を写す。書き換えが終わるまで待つ。リアルタイムのスレッドから呼ばないこと。
void StoreRead(Settings* settings, unsigned* version);

} // namespace eq
