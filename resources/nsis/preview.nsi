; Design preview only. No files, registry entries or shortcuts are installed.
Unicode true
RequestExecutionLevel user
Name "Heiran"
Caption "Heiran Design Preview"
OutFile "${HEIRAN_PREVIEW_OUTPUT}"
InstallDir "$LOCALAPPDATA\Heiran-Design-Preview"
ManifestDPIAware true
BrandingText "Heiran - design preview"

!include "MUI.nsh"
!define MUI_ICON "..\heiran.ico"
!define MUI_HEADERIMAGE
!define HEIRAN_NSIS_ASSET_DIR "."
!include "appearance.nsh"

!ifdef HEIRAN_PREVIEW_FINISH
  !insertmacro MUI_PAGE_FINISH
!else
  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
!endif
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Japanese"

Section "Heiran" Application
  SectionIn RO
SectionEnd
Section ".hrn (Heiran session)" AssociationHrn
SectionEnd
Section /o "Common image formats" AssociationImage
SectionEnd
Section "High precision formats" AssociationHdr
SectionEnd

Function .onInit
  !ifdef HEIRAN_PREVIEW_ENGLISH
    StrCpy $LANGUAGE ${LANG_ENGLISH}
  !else
    StrCpy $LANGUAGE ${LANG_JAPANESE}
  !endif
FunctionEnd
