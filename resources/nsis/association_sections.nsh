!include "${HEIRAN_NSIS_ASSET_DIR}\association_formats.nsh"

!macro HeiranAssociationSection EXT
  Section ".${EXT}" association_${EXT}
  SectionEnd
!macroend

Section "$(HeiranAssociationHrnName)" association_hrn
SectionEnd
SectionGroup "$(HeiranAssociationImageName)" association_common_group
  !insertmacro HeiranCommonImageFormats HeiranAssociationSection
SectionGroupEnd
SectionGroup "$(HeiranAssociationHdrName)" association_precision_group
  !insertmacro HeiranHighPrecisionFormats HeiranAssociationSection
SectionGroupEnd
