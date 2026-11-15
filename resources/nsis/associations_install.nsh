; Register only the file types selected on the association page.
!include "${HEIRAN_NSIS_ASSET_DIR}\association_selection.nsh"

!macro HeiranRegisterAssociation EXT
  !if "${EXT}" == "hrn"
    WriteRegStr SHCTX "Software\Classes\Heiran.Session" "" "Heiran Session"
    WriteRegStr SHCTX "Software\Classes\Heiran.Session\DefaultIcon" "" "$INSTDIR\bin\heiran.exe,0"
    WriteRegStr SHCTX "Software\Classes\Heiran.Session\shell\open\command" "" '"$INSTDIR\bin\heiran.exe" "%1"'
    WriteRegStr SHCTX "Software\Classes\.hrn" "" "Heiran.Session"
    WriteRegStr SHCTX "Software\Classes\.hrn\OpenWithProgids" "Heiran.Session" ""
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".hrn" "Heiran.Session"
  !else
    WriteRegStr SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "Heiran.Image" ""
    WriteRegStr SHCTX "Software\Heiran\Capabilities\FileAssociations" ".${EXT}" "Heiran.Image"
  !endif
!macroend

!insertmacro HeiranForEachSelectedAssociation HeiranRegisterAssociation

  ; ---- Shared registration ----
  ; Shared application entries have no per-extension default handlers.
  WriteRegStr SHCTX "Software\Classes\Heiran.Image" "" "Image"
  WriteRegStr SHCTX "Software\Classes\Heiran.Image\DefaultIcon" "" "$INSTDIR\bin\heiran.exe,0"
  WriteRegStr SHCTX "Software\Classes\Heiran.Image\shell\open\command" "" '"$INSTDIR\bin\heiran.exe" "%1"'

  ; Lets the user pick Heiran in Settings > Default apps.
  WriteRegStr SHCTX "Software\Heiran\Capabilities" "ApplicationName" "Heiran"
  WriteRegStr SHCTX "Software\Heiran\Capabilities" "ApplicationDescription" "Image viewer that shows several images in sync."
  WriteRegStr SHCTX "Software\RegisteredApplications" "Heiran" "Software\Heiran\Capabilities"

  ; Makes Explorer notice the new associations without a sign out.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
