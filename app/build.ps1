# 画面のアプリ（Tauri 2）をビルドする。Windows 側の PowerShell で動かす。
#   build.ps1          テストを走らせてから、リリース版をビルドする
#   build.ps1 -Run     ビルドのあと、アプリを起動する
# 出力: ユーザーの「ダウンロード」フォルダの MiniTone.exe（インストールせずに画面だけを試すためのもの）
param([switch] $Run)

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
New-Item -ItemType Directory -Force -Path $stage | Out-Null
# ビルドの出力は Windows 側に置く。WSL のフォルダに置くと遅く、ファイルのロックで失敗しやすい。
$target = Join-Path $stage 'target'

# cmd は UNC パス（WSL のフォルダ）をカレントにできないので、pushd で一時的にドライブを割り当てる。
$bat = Join-Path $env:TEMP 'minitone_app_build.bat'
$lines = @(
    '@echo off',
    "pushd `"$root\src-tauri`" || exit /b 1",
    "set CARGO_TARGET_DIR=$target",
    'cargo test --release || (popd & exit /b 1)',
    'cargo build --release',
    'set RESULT=%ERRORLEVEL%',
    'popd',
    'exit /b %RESULT%'
)
Set-Content -Path $bat -Value $lines -Encoding ASCII
cmd.exe /d /c $bat
$exit = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
if ($exit -ne 0) { throw "ビルドかテストに失敗した（exit=$exit）。" }

$exe = Join-Path (Get-DownloadsFolder) 'MiniTone.exe'
# 動いている版があると置き換えられないので、先に止める。
Get-Process -Name MiniTone -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
Start-Sleep -Milliseconds 300
Copy-Item (Join-Path $target 'release\minitone.exe') $exe -Force
Get-Item $exe | ForEach-Object { '{0} ({1} bytes)' -f $_.FullName, $_.Length }

if ($Run) { Start-Process -FilePath $exe }
