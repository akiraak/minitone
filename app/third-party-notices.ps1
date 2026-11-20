# 同梱しているクレート（Rust のライブラリ）の権利の表記を 1 つのファイルに集める。
# app\build.ps1 と app\build-installer.ps1 が、ビルドの前に呼ぶ。インストーラはこのファイルを同梱する。
# 出力: app\src-tauri\gen\THIRD-PARTY-NOTICES.txt（コミットしない）
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Join-Path $root 'src-tauri\Cargo.toml'
$outDir = Join-Path $root 'src-tauri\gen'
$outFile = Join-Path $outDir 'THIRD-PARTY-NOTICES.txt'

# Windows 向けのビルドに入るクレートだけを数える。ビルドのときにだけ使うクレートも含まれる（多めに載せる）。
$text = (& cargo metadata --format-version 1 --locked --filter-platform x86_64-pc-windows-msvc --manifest-path $manifest) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "cargo metadata failed (exit=$LASTEXITCODE)." }

# PowerShell 5.1 の ConvertFrom-Json は 2 MB までしか読めないので、上限の無い読み方をする。
Add-Type -AssemblyName System.Web.Extensions
$serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
$serializer.MaxJsonLength = [int]::MaxValue
$metadata = $serializer.DeserializeObject($text)

# 同じ文面（Apache-2.0 の本文など）は 1 回だけ載せ、使っているクレートを並べる。
$groups = [ordered]@{}
$withoutFile = @()
$count = 0
foreach ($package in ($metadata['packages'] | Sort-Object { $_['name'] }, { $_['version'] })) {
    if (-not $package['source']) { continue }   # このリポジトリ自身
    $count++
    $label = '{0} {1} ({2})' -f $package['name'], $package['version'], $package['license']
    $dir = Split-Path -Parent $package['manifest_path']
    $files = @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -match '^(LICEN[SC]E|COPYING|NOTICE|UNLICENSE|COPYRIGHT)' } | Sort-Object Name)
    if ($files.Count -eq 0) { $withoutFile += $label; continue }
    foreach ($file in $files) {
        $body = ([IO.File]::ReadAllText($file.FullName) -replace "`r`n", "`n").Trim()
        if (-not $body) { continue }
        $key = ($body -replace '\s+', ' ')
        if (-not $groups.Contains($key)) { $groups[$key] = @{ Body = $body; Users = New-Object System.Collections.Generic.List[string] } }
        if (-not $groups[$key].Users.Contains($label)) { $groups[$key].Users.Add($label) }
    }
}

$line = '=' * 78
$out = New-Object System.Text.StringBuilder
[void]$out.AppendLine('MiniTone includes the following third-party software.')
[void]$out.AppendLine('This file is generated from the Rust crates the app is built with (app/third-party-notices.ps1).')
[void]$out.AppendLine('')
foreach ($group in $groups.Values) {
    [void]$out.AppendLine($line)
    foreach ($user in $group.Users) { [void]$out.AppendLine($user) }
    [void]$out.AppendLine($line)
    [void]$out.AppendLine('')
    [void]$out.AppendLine($group.Body)
    [void]$out.AppendLine('')
}
if ($withoutFile.Count -gt 0) {
    [void]$out.AppendLine($line)
    [void]$out.AppendLine('The following crates do not ship a license file. Their license is the one named in parentheses.')
    [void]$out.AppendLine($line)
    foreach ($label in $withoutFile) { [void]$out.AppendLine($label) }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
[IO.File]::WriteAllText($outFile, ($out.ToString() -replace "`r`n", "`n" -replace "`n", "`r`n"), (New-Object Text.UTF8Encoding($false)))
'{0}: {1} crates, {2} license texts, {3} without a license file' -f $outFile, $count, $groups.Count, $withoutFile.Count
