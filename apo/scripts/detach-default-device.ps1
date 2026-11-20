# 開発用: いまの既定の出力デバイスからだけ、APO を外す。管理者の PowerShell で動かす。
# 「あとからつないだ、まだ APO が付いていないデバイス」の状態を作って、画面の知らせを試すためのもの。
# APO の登録そのものや、ほかのデバイスには触らない。元に戻すには、画面の「効くようにする」か
# install-apo.ps1 -AllDevices を使う。
#
#   detach-default-device.ps1          外す
#   detach-default-device.ps1 -DryRun  何も変えずに、行う変更だけを表示する（管理者でなくてよい）
param(
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$Clsid = '{40874C30-A23F-4E8D-B383-B40845BA5584}'
$FxGuid = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}'
$RenderPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = (New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $DryRun -and -not $isAdmin) {
    throw '管理者の PowerShell で実行すること（-DryRun は管理者でなくてよい）。'
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace EqDetach {
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
$id = [EqDetach.DefaultDevice]::GetId()
if (-not $id) { throw '既定の出力デバイスが見つからない。' }
$endpointId = ($id -split '\.')[-1]

$path = "$RenderPath\$endpointId\FxProperties"
$readKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($path, $false)
if (-not $readKey) { 'このデバイスには FxProperties が無い。何もしない。'; return }

$actions = @()
foreach ($valueName in $readKey.GetValueNames()) {
    if ($valueName -notlike "$FxGuid,*") { continue }
    $kind = $readKey.GetValueKind($valueName)
    if ($kind -eq [Microsoft.Win32.RegistryValueKind]::MultiString) {
        $current = @($readKey.GetValue($valueName))
        if ($current -notcontains $Clsid) { continue }
        $actions += @{ ValueName = $valueName; Rest = @($current | Where-Object { $_ -ine $Clsid }) }
    } elseif ($kind -eq [Microsoft.Win32.RegistryValueKind]::String) {
        if ([string]$readKey.GetValue($valueName) -ine $Clsid) { continue }
        $actions += @{ ValueName = $valueName; Rest = @() }
    }
}
$readKey.Close()
if ($actions.Count -eq 0) { 'いまの既定の出力デバイスには、この APO は付いていない。何もしない。'; return }

$writeKey = $null
if (-not $DryRun) {
    $writeKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
        $path,
        [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
        [System.Security.AccessControl.RegistryRights]'QueryValues, SetValue')
}
foreach ($action in $actions) {
    if ($action.Rest.Count -eq 0) {
        "$($action.ValueName): 値を消す"
        if ($writeKey) { $writeKey.DeleteValue($action.ValueName, $false) }
    } else {
        "$($action.ValueName): $($action.Rest -join ', ') に戻す"
        if ($writeKey) {
            $writeKey.SetValue($action.ValueName, [string[]]$action.Rest, [Microsoft.Win32.RegistryValueKind]::MultiString)
        }
    }
}
if ($writeKey) { $writeKey.Close() }

if ($DryRun) { ''; '-DryRun なので何も変えていない。'; return }

Restart-Service -Name audiosrv -Force
''
'いまの既定の出力デバイスから APO を外した。画面に「まだ効きません」の知らせが出るはず。'
