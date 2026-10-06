; openbus Windows installer (NSIS MUI2)
; First dialog: installer UI language (English / Chinese / German / ...),
; same pattern as 7-Zip, Git for Windows, and CMake.
; Choice is written to %APPDATA%\openbus\openbus\settings.json as ui.language
; so the Qt app starts in that locale (Settings can still change it later).
;
; Compile:
;   makensis /DVERSION=1.10.4 /DSOURCE_DIR=C:\staged /DOUT_FILE=C:\out\setup.exe installer\openbus.nsi

!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SOURCE_DIR
  !error "SOURCE_DIR must be set (staged portable folder containing openbus.exe)"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "openbus-${VERSION}-windows-x64-setup.exe"
!endif

Name "openbus"
OutFile "${OUT_FILE}"
Unicode True
InstallDir "$PROGRAMFILES64\openbus"
InstallDirRegKey HKLM "Software\openbus" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show

!ifndef LICENSE_FILE
  !define LICENSE_FILE "..\LICENSE"
!endif

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language dialog is shown automatically when more than one language is listed.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "Korean"

; Map NSIS $LANGUAGE to TranslationManager codes.
Function GetUiLanguageCode
  ${If} $LANGUAGE = ${LANG_SIMPCHINESE}
    StrCpy $0 "zh_CN"
  ${ElseIf} $LANGUAGE = ${LANG_TRADCHINESE}
    StrCpy $0 "zh_TW"
  ${ElseIf} $LANGUAGE = ${LANG_GERMAN}
    StrCpy $0 "de"
  ${ElseIf} $LANGUAGE = ${LANG_FRENCH}
    StrCpy $0 "fr"
  ${ElseIf} $LANGUAGE = ${LANG_SPANISH}
    StrCpy $0 "es"
  ${ElseIf} $LANGUAGE = ${LANG_JAPANESE}
    StrCpy $0 "ja"
  ${ElseIf} $LANGUAGE = ${LANG_PORTUGUESEBR}
    StrCpy $0 "pt_BR"
  ${ElseIf} $LANGUAGE = ${LANG_RUSSIAN}
    StrCpy $0 "ru"
  ${ElseIf} $LANGUAGE = ${LANG_KOREAN}
    StrCpy $0 "ko"
  ${Else}
    StrCpy $0 "en"
  ${EndIf}
FunctionEnd

Function PersistUiLanguage
  Call GetUiLanguageCode
  StrCpy $1 "$APPDATA\openbus\openbus"
  CreateDirectory "$1"
  IfFileExists "$1\settings.json" persist_done persist_write
persist_write:
  FileOpen $2 "$1\settings.json" w
  FileWrite $2 '{"ui.language": "$0"}'
  FileClose $2
persist_done:
FunctionEnd

Section "openbus"
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\openbus" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "DisplayName" "openbus"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "Publisher" "openbus"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "DisplayIcon" "$INSTDIR\openbus.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus" \
      "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\openbus"
  CreateShortCut "$SMPROGRAMS\openbus\openbus.lnk" "$INSTDIR\openbus.exe"
  CreateShortCut "$DESKTOP\openbus.lnk" "$INSTDIR\openbus.exe"

  Call PersistUiLanguage
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\openbus\openbus.lnk"
  RMDir "$SMPROGRAMS\openbus"
  Delete "$DESKTOP\openbus.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\openbus"
  DeleteRegKey HKLM "Software\openbus"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR"
SectionEnd
