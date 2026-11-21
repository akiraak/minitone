# 使う人向けのインストーラ（setup.exe）をビルドする。Windows 側の PowerShell で動かす。
# 先に apo\build.ps1 で APO の DLL をビルドしておくこと（インストーラに同梱する）。
#
# 要るもの: Tauri の CLI（バージョン 2）。全体には入れず、作業フォルダに入れてある:
#   cargo install tauri-cli --version "^2" --locked --root "$env:LOCALAPPDATA\akiraak-minitone-dev\tools"
# NSIS 本体は、Tauri の CLI が初回に %LOCALAPPDATA%\tauri へ取ってくる。
#
# 出力: ユーザーの「ダウンロード」フォルダの MiniTone-<バージョン>-setup.exe
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

# 実行して試すファイル（インストーラ、画面のアプリ）は、ユーザーの「ダウンロード」フォルダに置く。
# エクスプローラーからすぐ開ける場所にするため。場所を移している場合に備えて、Windows に尋ねる。
function Get-DownloadsFolder {
    try {
        $path = (New-Object -ComObject Shell.Application).NameSpace('shell:Downloads').Self.Path
        if ($path -and (Test-Path $path)) { return $path }
    } catch { }
    return (Join-Path $env:USERPROFILE 'Downloads')
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$stage = Join-Path $env:LOCALAPPDATA 'akiraak-minitone-dev'
$target = Join-Path $stage 'target'
$tauri = Join-Path $stage 'tools\bin\cargo-tauri.exe'
if (-not (Test-Path $tauri)) { throw "Tauri の CLI が無い: $tauri（このファイルの先頭のコマンドで入れる）" }

$dll = Join-Path (Split-Path -Parent $root) 'apo\build\minitone_apo.dll'
if (-not (Test-Path $dll)) { throw "APO の DLL が無い: $dll（先に apo\build.ps1 を実行する）" }

# 同梱しているクレートの権利の表記を集める（インストーラに入れる。ビルドは、このファイルがあることを確かめる）。
& (Join-Path $root 'third-party-notices.ps1')

# cmd は UNC パス（WSL のフォルダ）をカレントにできないので、pushd で一時的にドライブを割り当てる。
$bat = Join-Path $env:TEMP 'minitone_installer_build.bat'
$lines = @(
    '@echo off',
    "pushd `"$root\src-tauri`" || exit /b 1",
    "set CARGO_TARGET_DIR=$target",
    "`"$tauri`" tauri build --bundles nsis",
    'set RESULT=%ERRORLEVEL%',
    'popd',
    'exit /b %RESULT%'
)
Set-Content -Path $bat -Value $lines -Encoding ASCII
cmd.exe /d /c $bat
$exit = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
if ($exit -ne 0) { throw "インストーラのビルドに失敗した（exit=$exit）。" }

$setup = Get-ChildItem (Join-Path $target 'release\bundle\nsis') -Filter '*-setup.exe' |
    Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $setup) { throw 'できたはずのインストーラが見つからない。' }
# ファイル名にバージョンを入れる（MiniTone-0.2.3-setup.exe）。どの版かが名前で分かるように。
$version = (Get-Content (Join-Path $PSScriptRoot 'src-tauri\tauri.conf.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
$out = Join-Path (Get-DownloadsFolder) "MiniTone-$version-setup.exe"
Copy-Item $setup.FullName $out -Force
Get-Item $out | ForEach-Object { '{0} ({1} bytes)' -f $_.FullName, $_.Length }
