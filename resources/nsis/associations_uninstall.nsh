; Undo what associations_install.nsh wrote.
;
; The per-extension OpenWithProgids values are removed one by one rather than by
; deleting the extension keys. Those keys belong to whatever else handles the
; format, and deleting them would break the other application.

  DeleteRegValue SHCTX "Software\Classes\.png\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.jpg\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.jpeg\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.bmp\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.gif\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.webp\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.tif\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.tiff\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.avif\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.jxl\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.psd\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.exr\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.hdr\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.dpx\OpenWithProgids" "Heiran.Image"
  DeleteRegValue SHCTX "Software\Classes\.tga\OpenWithProgids" "Heiran.Image"

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
