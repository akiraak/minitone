# 開発用: install-apo.ps1 が行った変更を元に戻す。管理者の PowerShell で動かす。
#
#   uninstall-apo.ps1          元に戻す
#   uninstall-apo.ps1 -DryRun  何も変えずに、行う変更だけを表示する（管理者でなくてよい）
#
# 行うこと:
#   - 全部の出力デバイスの FxProperties から、この APO の CLSID を取り除く（置き換えた値は元の APO に戻す）
#   - COM と APO の登録を消す（regsvr32 /u）
#   - DisableProtectedAudioDG を、入れる前の状態に戻す
#   - 音のサービス（audiosrv）を再起動する。再生中の音は一度止まる
#   - Program Files の DLL を消す
# ProgramData の控え（入れる前のレジストリの書き出し）は残す。
param(
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

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $DryRun -and -not (Test-Admin)) {
    throw 'Run this in an administrator PowerShell (-DryRun does not need administrator rights).'
}

$state = $null
if (Test-Path $StateFile) { $state = Get-Content $StateFile -Raw | ConvertFrom-Json }

# install-apo.ps1 が新しく作った値（処理モードの指定と、古い形のデバイスに作った並べる形の FX の値）。記録から探す。
# 記録は、登録をやり直しても積み上げてある（CreatedValues）。古い版の記録（LastPlans）も読む。
$createdModes = @{}
if ($state) {
    foreach ($entry in @($state.CreatedValues)) {
        if ($entry) { $createdModes[[string]$entry] = $true }
    }
    foreach ($plan in @($state.LastPlans)) {
        foreach ($change in @($plan.Changes)) {
            if ($change -and $change.ValueName -like "$ModesGuid,*" -and -not $change.Existed) {
                $createdModes["$($plan.Endpoint)|$($change.ValueName)"] = $true
            }
        }
    }
}

# install-apo.ps1 が、古い形の値をこの APO に置き換えたもの。元の APO に戻す（"デバイス|値の名前" → 元の CLSID）。
$replaced = @{}
if ($state) {
    foreach ($entry in @($state.ReplacedValues)) {
        if (-not $entry) { continue }
        $parts = ([string]$entry) -split '\|'
        if ($parts.Count -eq 3) { $replaced["$($parts[0])|$($parts[1])"] = $parts[2] }
    }
}

# 1. 全部の出力デバイスから CLSID を取り除く
$render = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($RenderPath, $false)
$endpointIds = $render.GetSubKeyNames()
$render.Close()
$touched = 0
foreach ($endpointId in $endpointIds) {
    $path = "$RenderPath\$endpointId\FxProperties"
    $readKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($path, $false)
    if (-not $readKey) { continue }

    $actions = @()
    foreach ($valueName in $readKey.GetValueNames()) {
        if ($valueName -notlike "$FxGuid,*") { continue }
        $kind = $readKey.GetValueKind($valueName)
        if ($kind -eq [Microsoft.Win32.RegistryValueKind]::MultiString) {
            $current = @($readKey.GetValue($valueName))
            if ($current -notcontains $Clsid) { continue }
            $rest = @($current | Where-Object { $_ -ine $Clsid })
            # install-apo.ps1 が作った値（古い形のデバイスに作った、並べる形の値）は、元は無かったので消す。
            if ($createdModes.ContainsKey("$endpointId|$valueName")) { $rest = @() }
            $actions += @{ ValueName = $valueName; Rest = $rest; Kind = $kind }
        } elseif ($kind -eq [Microsoft.Win32.RegistryValueKind]::String) {
            if ([string]$readKey.GetValue($valueName) -ine $Clsid) { continue }
            $rest = @()
            if ($replaced.ContainsKey("$endpointId|$valueName")) { $rest = @($replaced["$endpointId|$valueName"]) }
            $actions += @{ ValueName = $valueName; Rest = $rest; Kind = $kind }
        }
    }
    $modesToDelete = @{}
    foreach ($valueName in $readKey.GetValueNames()) {
        if ($createdModes.ContainsKey("$endpointId|$valueName") -and $actions.Count -gt 0 -and
            -not ($actions | Where-Object { $_.ValueName -ieq $valueName })) {
            $modesToDelete[$valueName] = $true
        }
    }
    # 記録が無くても、install-apo.ps1 が作ったと分かる場合は消す（古い版は、登録をやり直すと記録を失っていた）。
    # 条件: この APO だけが入っていた MFX の値（= このスクリプトが消す値）があり、ほかに MFX の APO が無く、
    # 処理モードの値が「既定のモードだけ」になっている。install-apo.ps1 が書く内容そのもの。
    $mfxName = "$FxGuid,14"
    $modesName = "$ModesGuid,6"
    $createdMfx = @($actions | Where-Object { $_.ValueName -ieq $mfxName -and $_.Rest.Count -eq 0 }).Count -gt 0
    $names = $readKey.GetValueNames()
    if ($createdMfx -and ($names -contains $modesName) -and -not ($names -contains "$FxGuid,6")) {
        if ($readKey.GetValueKind($modesName) -eq [Microsoft.Win32.RegistryValueKind]::MultiString) {
            $modes = @($readKey.GetValue($modesName))
            if ($modes.Count -eq 1 -and $modes[0] -ieq $DefaultMode) { $modesToDelete[$modesName] = $true }
        }
    }
    foreach ($valueName in $modesToDelete.Keys) {
        $actions += @{ ValueName = $valueName; Rest = @(); Kind = 'CreatedModes' }
    }
    $readKey.Close()
    if ($actions.Count -eq 0) { continue }

    $touched++
    "Device: $endpointId"
    $writeKey = $null
    if (-not $DryRun) {
        $writeKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
            $path,
            [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
            [System.Security.AccessControl.RegistryRights]'QueryValues, SetValue')
    }
    foreach ($action in $actions) {
        if ($action.Rest.Count -eq 0) {
            # この APO だけが入っていた値は、install-apo.ps1 が作ったものなので消す。
            "  $($action.ValueName): delete the value"
            if ($writeKey) { $writeKey.DeleteValue($action.ValueName, $false) }
        } else {
            "  $($action.ValueName): restore to $($action.Rest -join ', ')"
            if ($writeKey) {
                if ($action.Kind -eq [Microsoft.Win32.RegistryValueKind]::String) {
                    $writeKey.SetValue($action.ValueName, [string]$action.Rest[0], [Microsoft.Win32.RegistryValueKind]::String)
                } else {
                    $writeKey.SetValue($action.ValueName, [string[]]$action.Rest, [Microsoft.Win32.RegistryValueKind]::MultiString)
                }
            }
        }
    }
    if ($writeKey) { $writeKey.Close() }
}
if ($touched -eq 0) { 'No output device has this APO attached.' }

# 2. COM と APO の登録を消す
$installedDll = Join-Path $InstallDir 'minitone_apo.dll'
if (Test-Path $installedDll) {
    "Unregister: $installedDll"
    if (-not $DryRun) {
        $process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/u', '/s', "`"$installedDll`"" -Wait -PassThru
        if ($process.ExitCode -ne 0) { "  Warning: regsvr32 /u failed (exit=$($process.ExitCode))." }
    }
} else {
    "DLL not found ($installedDll). Skipping unregistration."
}

# 3. 設定のキーを消す
if (-not $DryRun) {
    [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKeyTree($SettingsPath, $false)
    $parent = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\akiraak', $true)
    if ($parent) {
        $empty = ($parent.SubKeyCount -eq 0 -and $parent.ValueCount -eq 0)
        $parent.Close()
        if ($empty) { [Microsoft.Win32.Registry]::LocalMachine.DeleteSubKey('SOFTWARE\akiraak', $false) }
    }
}

# 4. 署名の検査の設定を、入れる前の状態に戻す
if ($state) {
    if ($state.DisableProtectedAudioDGWasAbsent) {
        'DisableProtectedAudioDG: delete the value (it did not exist before the install)'
        if (-not $DryRun) {
            $audioKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($AudioPath, $true)
            $audioKey.DeleteValue('DisableProtectedAudioDG', $false)
            $audioKey.Close()
        }
    } else {
        "DisableProtectedAudioDG: restore to $($state.PreviousDisableProtectedAudioDG)"
        if (-not $DryRun) {
            $audioKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($AudioPath, $true)
            $audioKey.SetValue('DisableProtectedAudioDG', [int]$state.PreviousDisableProtectedAudioDG, [Microsoft.Win32.RegistryValueKind]::DWord)
            $audioKey.Close()
        }
    }
} else {
    'Warning: there is no record from the install, so DisableProtectedAudioDG is left as it is.'
}

if ($DryRun) { ''; '-DryRun: nothing was changed.'; return }

# 5. 音のサービスを再起動して、DLL を手放させる
Restart-Service -Name audiosrv -Force
Start-Sleep -Seconds 2

# 6. DLL と記録を消す
if (Test-Path $installedDll) {
    try {
        Remove-Item $installedDll -Force
        if (-not (Get-ChildItem $InstallDir)) { Remove-Item $InstallDir -Force }
    } catch {
        "Warning: could not delete the DLL ($installedDll). Delete it by hand after a restart."
    }
}
if (Test-Path $StateFile) { Remove-Item $StateFile -Force }

''
'Removed. Check that sound plays.'
"The registry backup from before the install is kept in $StateDir."
