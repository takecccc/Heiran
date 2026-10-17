; File associations, chosen on the component page of the installer.
;
; Windows 10 and later does not let an installer take over an extension that
; already has a default handler. Writing the association here puts Heiran into
; the "Open with" list and into the Default apps settings; the user still makes
; it the default from there. The .hrn extension has no other owner, so for that
; one the association takes effect straight away.
;
; SHCTX follows the install mode, so a per-machine install writes to HKLM and a
; per-user install writes to HKCU.

  ; ---- Heiran session files (.hrn) ----
  SectionGetFlags ${association_hrn} $R8
  IntOp $R8 $R8 & ${SF_SELECTED}
  IntCmp $R8 ${SF_SELECTED} 0 heiran_skip_hrn heiran_skip_hrn
    WriteRegStr SHCTX "Software\Classes\Heiran.Session" "" "Heiran Session"
    WriteRegStr SHCTX "Software\Classes\Heiran.Session\DefaultIcon" "" "$INSTDIR\bin\heiran.exe,0"
    WriteRegStr SHCTX "Software\Classes\Heiran.Session\shell\open\command" "" '"$INSTDIR\bin\heiran.exe" "%1"'
    WriteRegStr SHCTX "Software\Classes\.hrn" "" "Heiran.Session"
    WriteRegStr SHCTX "Software\Classes\.hrn\OpenWithProgids" "Heiran.Session" ""
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".hrn" "Heiran.Session"
  heiran_skip_hrn:

  ; ---- Common image formats ----
  SectionGetFlags ${association_image} $R8
  IntOp $R8 $R8 & ${SF_SELECTED}
  IntCmp $R8 ${SF_SELECTED} 0 heiran_skip_image heiran_skip_image
    WriteRegStr SHCTX "Software\Classes\.png\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.jpg\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.jpeg\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.bmp\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.gif\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.webp\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.tif\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.tiff\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.avif\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.jxl\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.psd\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".png" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".jpg" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".jpeg" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".bmp" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".gif" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".webp" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".tif" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".tiff" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".avif" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".jxl" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".psd" "Heiran.Image"
  heiran_skip_image:

  ; ---- High precision formats ----
  ; These rarely have a default handler, so the association usually takes hold.
  SectionGetFlags ${association_hdr} $R8
  IntOp $R8 $R8 & ${SF_SELECTED}
  IntCmp $R8 ${SF_SELECTED} 0 heiran_skip_hdr heiran_skip_hdr
    WriteRegStr SHCTX "Software\Classes\.exr\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.hdr\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.dpx\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Classes\.tga\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".exr" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".hdr" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".dpx" "Heiran.Image"
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".tga" "Heiran.Image"
  heiran_skip_hdr:

  ; ---- Shared registration ----
  ; The image ProgId and the application registration are written whenever any
  ; association was picked. Writing them unconditionally is harmless, and it
  ; keeps the branches above to the per-extension entries.
  WriteRegStr SHCTX "Software\Classes\Heiran.Image" "" "Image"
  WriteRegStr SHCTX "Software\Classes\Heiran.Image\DefaultIcon" "" "$INSTDIR\bin\heiran.exe,0"
  WriteRegStr SHCTX "Software\Classes\Heiran.Image\shell\open\command" "" '"$INSTDIR\bin\heiran.exe" "%1"'

  ; Lets the user pick Heiran in Settings > Default apps.
  WriteRegStr SHCTX "Software\Heiran\Capabilities" "ApplicationName" "Heiran"
  WriteRegStr SHCTX "Software\Heiran\Capabilities" "ApplicationDescription" "Image viewer that shows several images in sync."
  WriteRegStr SHCTX "Software\RegisteredApplications" "Heiran" "Software\Heiran\Capabilities"

  ; Makes Explorer notice the new associations without a sign out.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
