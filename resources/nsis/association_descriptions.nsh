!macro HeiranAssociationDescription EXT
  !insertmacro MUI_DESCRIPTION_TEXT ${association_${EXT}} "$(HeiranAssociationExtensionDescription)"
!macroend

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${association_hrn} "$(HeiranAssociationHrnDescription)"
  !insertmacro MUI_DESCRIPTION_TEXT ${association_common_group} "$(HeiranAssociationImageDescription)"
  !insertmacro HeiranCommonImageFormats HeiranAssociationDescription
  !insertmacro MUI_DESCRIPTION_TEXT ${association_precision_group} "$(HeiranAssociationHdrDescription)"
  !insertmacro HeiranHighPrecisionFormats HeiranAssociationDescription
!insertmacro MUI_FUNCTION_DESCRIPTION_END
