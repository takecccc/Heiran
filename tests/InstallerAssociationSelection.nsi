Unicode true
RequestExecutionLevel user
Name "Heiran association selection test"
OutFile "${HEIRAN_TEST_EXECUTABLE}"
!include "MUI.nsh"
!include "Sections.nsh"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Japanese"
!include "${HEIRAN_NSIS_ASSET_DIR}\localization.nsh"
!include "${HEIRAN_NSIS_ASSET_DIR}\association_sections.nsh"
!include "${HEIRAN_NSIS_ASSET_DIR}\association_selection.nsh"

!macro HeiranRecordAssociation EXT
  FileWrite $R7 ".${EXT}$\r$\n"
!macroend
!macro HeiranDeselectAssociation EXT
  SectionSetFlags ${association_${EXT}} 0
!macroend

Function .onInit
  !ifdef HEIRAN_TEST_EXCLUDED
    !insertmacro HeiranDeselectAssociation hrn
    !insertmacro HeiranDeselectAssociation png
    !insertmacro HeiranDeselectAssociation jpeg
    !insertmacro HeiranDeselectAssociation exr
  !endif
  !ifdef HEIRAN_TEST_NONE
    !insertmacro HeiranDeselectAssociation hrn
    !insertmacro HeiranImageFormats HeiranDeselectAssociation
  !endif
  FileOpen $R7 "${HEIRAN_TEST_OUTPUT}" w
  !insertmacro HeiranForEachSelectedAssociation HeiranRecordAssociation
  FileClose $R7
  SetErrorLevel 0
  Quit
FunctionEnd
