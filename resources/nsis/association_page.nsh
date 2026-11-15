!define MUI_PAGE_HEADER_TEXT "$(HeiranAssociationsTitle)"
!define MUI_PAGE_HEADER_SUBTEXT "$(HeiranAssociationsSubtitle)"
!define MUI_COMPONENTSPAGE_TEXT_TOP "$(HeiranAssociationsIntro)"
!define MUI_COMPONENTSPAGE_TEXT_COMPLIST "$(HeiranAssociationsList)"
!define MUI_COMPONENTSPAGE_TEXT_DESCRIPTION_INFO "$(HeiranAssociationsHelp)"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW HeiranAssociationPageShow

Function HeiranAssociationPageShow
  FindWindow $R8 "#32770" "" $HWNDPARENT
  GetDlgItem $R8 $R8 1023
  SendMessage $R8 ${WM_SETTEXT} 0 "STR:"
  ShowWindow $R8 ${SW_HIDE}
FunctionEnd
