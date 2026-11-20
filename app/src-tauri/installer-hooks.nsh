; Hooks for the Tauri NSIS installer.
; They register / unregister the audio processing object (APO) with the same
; PowerShell scripts that are used during development (apo/scripts/).
;
; The installer is a 32-bit process. Starting powershell.exe from it normally
; runs the 32-bit PowerShell, whose HKLM\SOFTWARE writes are redirected to
; WOW6432Node. The APO must be registered in the 64-bit view, so the 64-bit
; PowerShell is started explicitly through the Sysnative alias.

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
