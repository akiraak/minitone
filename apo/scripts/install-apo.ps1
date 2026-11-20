# 開発用: APO を Windows に登録し、出力デバイスに取り付ける。管理者の PowerShell で動かす。
#
#   install-apo.ps1                      いまの既定の出力デバイスにだけ取り付ける（MFX の段）
#   install-apo.ps1 -AllDevices          有効な出力デバイス全部に取り付ける
#   install-apo.ps1 -Stage EFX           EFX の段に取り付ける（sx360 の Bluetooth のヘッドホンでは効かなかった）
#   install-apo.ps1 -DryRun              何も変えずに、行う変更だけを表示する（管理者でなくてよい）
#
# 変えるもの:
#   - Program Files に DLL を置く
#   - HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio の DisableProtectedAudioDG を 1 にする
#   - COM と APO の登録（regsvr32）
#   - 出力デバイスの FxProperties に、この APO の CLSID を足す
#   - ProgramData に設定のフォルダを作り、一般のユーザーが設定ファイルを書けるようにする
#   - 音のサービス（audiosrv）を再起動する。再生中の音は一度止まる
# 元に戻すには uninstall-apo.ps1 を使う。変える前の状態は ProgramData に控える。
param(
    [ValidateSet('EFX', 'MFX')] [string] $Stage = 'MFX',
    [switch] $AllDevices,
    [string] $DllPath,
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$Clsid = '{40874C30-A23F-4E8D-B383-B40845BA5584}'
$FxGuid = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}'
$ModesGuid = '{d3993a3f-99c2-4402-b5ec-a92a0367664b}'
$DefaultMode = '{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}'
$RenderPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$AudioPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Audio'
$SettingsPath = 'SOFTWARE\akiraak\minitone'
$InstallDir = Join-Path $env:ProgramFiles 'akiraak-minitone'
$StateDir = Join-Path $env:ProgramData 'akiraak-minitone'
$StateFile = Join-Path $StateDir 'install-state.json'
$ConfigDir = Join-Path $StateDir 'config'
$SettingsFile = Join-Path $ConfigDir 'settings.txt'

# 段ごとの値の番号。legacy は APO を 1 つだけ書く古い形、composite は複数を並べる形。
$Slots = @{
    EFX = @{ Legacy = 7; Composite = 15; Modes = $null }
    MFX = @{ Legacy = 6; Composite = 14; Modes = 6 }
}

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# 管理者は FxProperties の値を書けるが、キーの作成はできない。必要な権利だけを求めて開く。
function Open-FxKey([string] $endpointId, [bool] $writable) {
    $path = "$RenderPath\$endpointId\FxProperties"
    if ($writable) {
        return [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
            $path,
            [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
            [System.Security.AccessControl.RegistryRights]'QueryValues, SetValue')
    }
    return [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($path, $false)
}

function Get-EndpointName([string] $endpointId) {
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey("$RenderPath\$endpointId\Properties", $false)
    if (-not $key) { return $endpointId }
    $name = $key.GetValue('{a45c254e-df1c-4efd-8020-67d146a850e0},2')
    $adapter = $key.GetValue('{b3f8fa53-0004-438e-9003-51a46e139bfc},6')
    $key.Close()
    return "$name ($adapter)"
}

function Get-DefaultRenderEndpointId {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace EqInstall {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
    }
    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        int Activate(ref Guid iid, int clsCtx, IntPtr p, out IntPtr iface);
        int OpenPropertyStore(int access, out IntPtr props);
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    }
    public static class DefaultDevice {
        public static string GetId() {
            var e = (IMMDeviceEnumerator)(new MMDeviceEnumeratorCom());
            IMMDevice d;
            if (e.GetDefaultAudioEndpoint(0, 0, out d) != 0) return null;
            string id; d.GetId(out id); return id;
        }
    }
}
'@
    $id = [EqInstall.DefaultDevice]::GetId()
    if (-not $id) { throw 'No default output device was found.' }
    # "{0.0.0.00000000}.{GUID}" の後ろの GUID がレジストリのキーの名前。
    return ($id -split '\.')[-1]
}

function Get-ActiveRenderEndpointIds {
    $render = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($RenderPath, $false)
    $ids = @()
    foreach ($id in $render.GetSubKeyNames()) {
        $key = $render.OpenSubKey($id, $false)
        if ($key.GetValue('DeviceState') -eq 1) { $ids += $id }
        $key.Close()
    }
    $render.Close()
    return $ids
}

# 1 つのデバイスについて、行う変更を決める。何も書かない。
function Get-EndpointPlan([string] $endpointId) {
    $plan = [ordered]@{ Endpoint = $endpointId; Name = (Get-EndpointName $endpointId); Changes = @(); Skip = $null }
    $key = Open-FxKey $endpointId $false
    if (-not $key) {
        $plan.Skip = 'the device has no FxProperties key (it cannot be created, even as administrator)'
        return $plan
    }
    $names = $key.GetValueNames()
    $slot = $Slots[$Stage]
    $hasComposite = @($names | Where-Object { $_ -in "$FxGuid,13", "$FxGuid,14", "$FxGuid,15" }).Count -gt 0
    $hasLegacy = @($names | Where-Object { $_ -in "$FxGuid,5", "$FxGuid,6", "$FxGuid,7" }).Count -gt 0

    if ($hasComposite -or -not $hasLegacy) {
        # 複数を並べる形。ドライバの APO の前に置く（混ぜた直後の形式で受け取るため）。
        $valueName = "$FxGuid,$($slot.Composite)"
        $existed = $names -contains $valueName
        $previous = @()
        if ($existed) { $previous = @($key.GetValue($valueName)) }
        if ($previous -contains $Clsid) {
            $plan.Skip = 'already attached'
        } else {
            $plan.Changes += [ordered]@{
                ValueName = $valueName; Kind = 'MultiString'; Existed = $existed
                Previous = $previous; New = @($Clsid) + $previous
            }
        }
    } else {
        # APO を 1 つだけ書く古い形。
        $valueName = "$FxGuid,$($slot.Legacy)"
        if ($names -contains $valueName) {
            $current = [string]$key.GetValue($valueName)
            if ($current -ieq $Clsid) {
                $plan.Skip = 'already attached'
            } else {
                # 段がドライバの APO で埋まっている。古い形の値はそのままにして、複数を並べる形の値を作る。
                # 取り付ける段は「この APO、元の APO」の順。ほかの段は古い形の値を写す
                # （並べる形の値があると古い形が読まれなくなっても、ほかの段の APO が外れないように）。
                foreach ($other in @(@{ Legacy = 5; Composite = 13 }, @{ Legacy = 6; Composite = 14 }, @{ Legacy = 7; Composite = 15 })) {
                    $legacyName = "$FxGuid,$($other.Legacy)"
                    $compositeName = "$FxGuid,$($other.Composite)"
                    if ($other.Composite -eq $slot.Composite) {
                        $new = @($Clsid, $current)
                    } elseif ($names -contains $legacyName) {
                        $new = @([string]$key.GetValue($legacyName))
                    } else {
                        continue
                    }
                    $plan.Changes += [ordered]@{
                        ValueName = $compositeName; Kind = 'MultiString'; Existed = $false; Previous = @(); New = $new
                    }
                }
            }
        } else {
            $plan.Changes += [ordered]@{
                ValueName = $valueName; Kind = 'String'; Existed = $false; Previous = @(); New = @($Clsid)
            }
        }
    }

    # MFX は、どの処理モードで使うかの指定が要る。無ければ既定のモードを書く。
    if (-not $plan.Skip -and $slot.Modes) {
        $modesName = "$ModesGuid,$($slot.Modes)"
        if (-not ($names -contains $modesName)) {
            $plan.Changes += [ordered]@{
                ValueName = $modesName; Kind = 'MultiString'; Existed = $false; Previous = @(); New = @($DefaultMode)
            }
        }
    }
    $key.Close()
    return $plan
}

# ---- ここから本体 ----

if (-not $DryRun -and -not (Test-Admin)) {
    throw 'Run this in an administrator PowerShell (-DryRun does not need administrator rights).'
}

if (-not $DllPath) {
    # スクリプトと同じフォルダに DLL があればそれを、無ければビルドの出力を使う。
    $DllPath = Join-Path $PSScriptRoot 'minitone_apo.dll'
    if (-not (Test-Path $DllPath)) { $DllPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\minitone_apo.dll' }
}
if (-not (Test-Path $DllPath)) { throw "DLL not found: $DllPath (build it with build.ps1 first)" }

if ($AllDevices) { $targets = Get-ActiveRenderEndpointIds } else { $targets = @(Get-DefaultRenderEndpointId) }
$plans = @($targets | ForEach-Object { Get-EndpointPlan $_ })

$audioKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($AudioPath, $false)
$previousProtected = $audioKey.GetValue('DisableProtectedAudioDG')
$audioKey.Close()

"Stage: $Stage"
"DLL: $DllPath -> $InstallDir"
"DisableProtectedAudioDG: now=$(if ($null -eq $previousProtected) { 'absent' } else { $previousProtected }) -> 1"
foreach ($plan in $plans) {
    "Device: $($plan.Name)"
    if ($plan.Skip) { "  Skipped: $($plan.Skip)"; continue }
    foreach ($change in $plan.Changes) {
        "  $($change.ValueName)"
        "    before: $(if ($change.Existed) { $change.Previous -join ', ' } else { '(no value)' })"
        "    after:  $($change.New -join ', ')"
    }
}
"Settings file: $SettingsFile"

if ($DryRun) { ''; '-DryRun: nothing was changed.'; return }
# 取り付けるデバイスが無くても先へ進む（DLL の入れ替えと、設定のフォルダの用意のため）。

# 1. 変える前の状態を控える
New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
# APO（LocalService の権限で動く）がこのフォルダにログを書けるようにする。S-1-5-19 は LocalService。
& icacls.exe $StateDir /grant '*S-1-5-19:(OI)(CI)M' | Out-Null
# レジストリの控えは、入っていない状態から入れるときにだけ書き出す（1 回で約 6 MB ある）。
# 上書きで入れ直すたびに書き出すと、「入れる前」ではない控えが増えていくだけになる。
if (-not (Test-Path $StateFile)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    & reg.exe export "HKLM\$RenderPath" (Join-Path $StateDir "render-before-$stamp.reg") /y | Out-Null
}
if (Test-Path $StateFile) {
    # 2 回目以降の実行では、最初に控えた「入れる前」の状態を残す。
    $state = Get-Content $StateFile -Raw | ConvertFrom-Json
    $firstProtected = $state.PreviousDisableProtectedAudioDG
    $protectedWasAbsent = $state.DisableProtectedAudioDGWasAbsent
} else {
    $state = $null
    $firstProtected = $previousProtected
    $protectedWasAbsent = ($null -eq $previousProtected)
}

# 2. DLL を置く
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$installedDll = Join-Path $InstallDir 'minitone_apo.dll'
$sameDll = (Test-Path $installedDll) -and
    ((Get-FileHash $installedDll).Hash -eq (Get-FileHash $DllPath).Hash)
if (-not $sameDll) {
    try {
        Copy-Item $DllPath $installedDll -Force
    } catch {
        # 前の版を音のエンジン（audiodg.exe）が読み込んでいる。音のサービスを止めて手放させてから置く。
        # サービスは最後の手順（8）で起動し直す。
        'The previous version is in use. Stopping the audio service to replace it.'
        Stop-Service -Name audiosrv -Force
        $copied = $false
        foreach ($attempt in 1..10) {
            Start-Sleep -Milliseconds 500
            try { Copy-Item $DllPath $installedDll -Force; $copied = $true; break } catch { }
        }
        if (-not $copied) {
            Start-Service -Name audiosrv
            throw "Cannot write the DLL ($installedDll). Run uninstall-apo.ps1 and try again."
        }
    }
}

# 3. 署名の検査を切る
$audioKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($AudioPath, $true)
$audioKey.SetValue('DisableProtectedAudioDG', 1, [Microsoft.Win32.RegistryValueKind]::DWord)
$audioKey.Close()

# 4. COM と APO の登録
$process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/s', "`"$installedDll`"" -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "regsvr32 failed (exit=$($process.ExitCode))." }

# 5. 設定のフォルダを作る。画面（管理者でない）が設定ファイルを書けるよう、
#    このフォルダだけ一般のユーザーに書き込みを許す。S-1-5-32-545 は Users。
New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null
& icacls.exe $ConfigDir /grant '*S-1-5-32-545:(OI)(CI)M' | Out-Null
if (-not (Test-Path $SettingsFile)) {
    # 最初は切っておく（オリジナルの音）。
    Set-Content -Path $SettingsFile -Value @('enabled=0', 'preamp=0') -Encoding ASCII
}
# 前の版が使っていた、動作確認用の音量の値を消す。
[Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree($SettingsPath, $false)

# 6. 出力デバイスに取り付ける
foreach ($plan in $plans) {
    if ($plan.Skip) { continue }
    $key = Open-FxKey $plan.Endpoint $true
    foreach ($change in $plan.Changes) {
        if ($change.Kind -eq 'MultiString') {
            $key.SetValue($change.ValueName, [string[]]$change.New, [Microsoft.Win32.RegistryValueKind]::MultiString)
        } else {
            $key.SetValue($change.ValueName, [string]$change.New[0], [Microsoft.Win32.RegistryValueKind]::String)
        }
    }
    $key.Close()
}

# 7. 何を変えたかを書き残す（uninstall-apo.ps1 が読む）
# このスクリプトが新しく作った「処理モードの指定」の値は、CLSID を含まないので、解除のときに記録が要る。
# 登録をやり直しても失わないよう、前の記録に足していく（"デバイス|値の名前" の一覧）。
$createdValues = @()
if ($state) { $createdValues = @($state.CreatedValues | Where-Object { $_ }) }
foreach ($plan in $plans) {
    foreach ($change in @($plan.Changes)) {
        # 新しく作った、並べる形の値（,13〜,15）も記録する。解除のときに中身ごと消す
        # （古い形のデバイスでは、元の APO を写した値も作るため）。
        $createdFx = $change.ValueName -in "$FxGuid,13", "$FxGuid,14", "$FxGuid,15"
        $createdModes = $change.ValueName -like "$ModesGuid,*"
        if ($change -and -not $change.Existed -and ($createdFx -or $createdModes)) {
            $entry = "$($plan.Endpoint)|$($change.ValueName)"
            if ($createdValues -notcontains $entry) { $createdValues += $entry }
        }
    }
}
[ordered]@{
    InstalledAt = (Get-Date -Format 's')
    Stage = $Stage
    DisableProtectedAudioDGWasAbsent = $protectedWasAbsent
    PreviousDisableProtectedAudioDG = $firstProtected
    CreatedValues = $createdValues
    LastPlans = $plans
} | ConvertTo-Json -Depth 6 | Set-Content -Path $StateFile -Encoding UTF8

# 8. 音のサービスを再起動して反映する
if ((Get-Service -Name audiosrv).Status -eq 'Running') {
    Restart-Service -Name audiosrv -Force
} else {
    Start-Service -Name audiosrv
}

''
'Attached. Check that sound plays.'
"Log: $StateDir\minitone_apo.log"
"Backup: $StateDir"
'To undo, run uninstall-apo.ps1 in an administrator PowerShell.'
