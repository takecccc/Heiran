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
!define MUI_CUSTOMFUNCTION_GUIINIT HeiranPreviewGuiInit
!define MUI_ICON "..\heiran.ico"
!define MUI_HEADERIMAGE
!define HEIRAN_NSIS_ASSET_DIR "."
!include "appearance.nsh"

!ifdef HEIRAN_PREVIEW_FINISH
  !insertmacro MUI_PAGE_FINISH
!else ifdef HEIRAN_PREVIEW_ASSOCIATIONS
  !include "association_page.nsh"
  !insertmacro MUI_PAGE_COMPONENTS
!else
  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
  !include "association_page.nsh"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
!endif
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Japanese"
!include "localization.nsh"

Section "-Heiran" Application
  SectionIn RO
SectionEnd
Section "$(HeiranAssociationHrnName)" AssociationHrn
SectionEnd
Section /o "$(HeiranAssociationImageName)" AssociationImage
SectionEnd
Section "$(HeiranAssociationHdrName)" AssociationHdr
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${Application} "$(HeiranApplicationDescription)"
  !insertmacro MUI_DESCRIPTION_TEXT ${AssociationHrn} "$(HeiranAssociationHrnDescription)"
  !insertmacro MUI_DESCRIPTION_TEXT ${AssociationImage} "$(HeiranAssociationImageDescription)"
  !insertmacro MUI_DESCRIPTION_TEXT ${AssociationHdr} "$(HeiranAssociationHdrDescription)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  !ifdef HEIRAN_PREVIEW_ENGLISH
    StrCpy $LANGUAGE ${LANG_ENGLISH}
  !else
    StrCpy $LANGUAGE ${LANG_JAPANESE}
  !endif
FunctionEnd

Function HeiranPreviewGuiInit
  !ifdef HEIRAN_PREVIEW_UPGRADE
    ShowWindow $HWNDPARENT 5
    StrCpy $1 "Heiran"
    MessageBox MB_YESNOCANCEL|MB_ICONEXCLAMATION "$(HeiranUpgradePrompt)" /SD IDCANCEL
  !endif
FunctionEnd
