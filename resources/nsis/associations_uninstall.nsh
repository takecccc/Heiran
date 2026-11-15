; Undo what associations_install.nsh wrote.
;
; The per-extension OpenWithProgids values are removed one by one rather than by
; deleting the extension keys. Those keys belong to whatever else handles the
; format, and deleting them would break the other application.

!include "${HEIRAN_NSIS_ASSET_DIR}\association_formats.nsh"
!macro HeiranUnregisterImage EXT
  DeleteRegValue SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "Heiran.Image"
!macroend
!insertmacro HeiranImageFormats HeiranUnregisterImage

  ; .hrn is only ours, so the whole entry goes, but only while it still points
  ; at Heiran. Another application may have taken it over in the meantime.
  ReadRegStr $R8 SHCTX "Software\Classes\.hrn" ""
  StrCmp $R8 "Heiran.Session" 0 heiran_keep_hrn
    DeleteRegKey SHCTX "Software\Classes\.hrn"
  heiran_keep_hrn:

  DeleteRegKey SHCTX "Software\Classes\Heiran.Session"
  DeleteRegKey SHCTX "Software\Classes\Heiran.Image"
  DeleteRegValue SHCTX "Software\RegisteredApplications" "Heiran"
  DeleteRegKey SHCTX "Software\Heiran"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
