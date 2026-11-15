!include "${HEIRAN_NSIS_ASSET_DIR}\association_formats.nsh"

!macro HeiranSelectedAssociation EXT
  SectionGetFlags ${association_${EXT}} $R8
  IntOp $R8 $R8 & ${SF_SELECTED}
  IntCmp $R8 ${SF_SELECTED} 0 heiran_skip_${EXT} heiran_skip_${EXT}
    !insertmacro ${HEIRAN_ASSOCIATION_ACTION} ${EXT}
  heiran_skip_${EXT}:
!macroend

!macro HeiranForEachSelectedAssociation APPLY
  !define HEIRAN_ASSOCIATION_ACTION ${APPLY}
  !insertmacro HeiranSelectedAssociation hrn
  !insertmacro HeiranImageFormats HeiranSelectedAssociation
  !undef HEIRAN_ASSOCIATION_ACTION
!macroend
