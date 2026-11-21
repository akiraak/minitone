; Hooks for the Tauri NSIS installer.
; They register / unregister the audio processing object (APO) with the same
; PowerShell scripts that are used during development (apo/scripts/).
;
; The installer is a 32-bit process. Starting powershell.exe from it normally
; runs the 32-bit PowerShell, whose HKLM\SOFTWARE writes are redirected to
; WOW6432Node. The APO must be registered in the 64-bit view, so the 64-bit
; PowerShell is started explicitly through the Sysnative alias.

!include "x64.nsh"

; Version 0.1.0 deleted HKLM\Software\akiraak\MiniTone after every install
; (install-apo.ps1 used the same key for an old test value). The installer keeps
; the install location there. Without it, "Uninstall before installing" starts
; the old uninstaller with an empty `_?=` and it stops with
; "Error launching installer".
; Before any page is shown, put the install location back from the uninstall
; entry. .onInit has already selected the 64-bit registry view.
!define MUI_CUSTOMFUNCTION_GUIINIT MiniToneRepairInstallLocation
Function MiniToneRepairInstallLocation
  Push $R0
  Push $R1
  ReadRegStr $R0 HKLM "Software\akiraak\MiniTone" ""
  ${If} $R0 == ""
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiniTone" "InstallLocation"
    ; The value is written with quotes around it.
    StrCpy $R1 $R0 1
    ${If} $R1 == '"'
      StrCpy $R0 $R0 "" 1
      StrCpy $R0 $R0 -1
    ${EndIf}
    ${If} $R0 != ""
    ${AndIf} ${FileExists} "$R0\uninstall.exe"
      WriteRegStr HKLM "Software\akiraak\MiniTone" "" $R0
    ${EndIf}
  ${EndIf}
  Pop $R1
  Pop $R0
FunctionEnd

; The APO is an x64 DLL. The audio engine of Windows on Arm cannot load it,
; so the installer stops there before it changes anything.
!macro NSIS_HOOK_PREINSTALL
  ${If} ${IsNativeARM64}
    MessageBox MB_OK|MB_ICONSTOP "MiniTone supports only x64 Windows. It does not work on Windows on Arm."
    Abort
  ${EndIf}
!macroend

!macro EQ_FIND_POWERSHELL
  StrCpy $R8 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$R8"
    StrCpy $R8 "$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}
!macroend

!macro NSIS_HOOK_POSTINSTALL
  !insertmacro EQ_FIND_POWERSHELL
  DetailPrint "Registering the audio processing object..."
  nsExec::ExecToLog '"$R8" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\apo\install-apo.ps1" -AllDevices'
  Pop $R9
  ${If} $R9 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "The audio processing object could not be registered (exit code $R9).$\r$\nThe app is installed, but it will not change the sound.$\r$\nDetails: $INSTDIR\apo\install-apo.ps1"
  ${EndIf}
!macroend

!macro NSIS_HOOK_PREUNINSTALL
  !insertmacro EQ_FIND_POWERSHELL
  DetailPrint "Removing the audio processing object..."
  nsExec::ExecToLog '"$R8" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\apo\uninstall-apo.ps1"'
  Pop $R9
  ${If} $R9 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "The audio processing object could not be removed cleanly (exit code $R9)."
  ${EndIf}
!macroend
