const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\';
  LegacyKey = UninstallKey + 'Heiran';
  InstallerKey = UninstallKey + 'Heiran_is1';
  LegacyUninstaller = 'Uninstall.exe';
  HrnKey = 'Software\Classes\.hrn';
  SessionProgId = 'Heiran.Session';
  RuntimeKey = 'Software\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

procedure InitializeWizard();
begin
  WizardForm.TasksList.BorderStyle := bsSingle;
end;

function LegacyInstallDir(): String;
var
  Command, Icon, Directory: String;
begin
  Result := '';
  { The 32-bit NSIS installer registered its uninstaller in the 32-bit view. }
  if not RegQueryStringValue(HKLM32, LegacyKey, 'UninstallString', Command) then
    Exit;
  if not RegQueryStringValue(HKLM32, LegacyKey, 'DisplayIcon', Icon) then
    Exit;
  Directory := ExtractFileDir(RemoveQuotes(Command));
  if (Directory <> '') and
     (CompareText(Command, '"' + AddBackslash(Directory) + LegacyUninstaller + '"') = 0) and
     (CompareText(Icon, AddBackslash(Directory) + 'bin\heiran.exe') = 0) then
    Result := Directory;
end;

function HeiranDefaultDir(Param: String): String;
begin
  Result := '';
  if IsAdminInstallMode then
    Result := LegacyInstallDir();
  if Result = '' then
    Result := ExpandConstant('{autopf}\Heiran');
end;

function CanSetHrnDefault(): Boolean;
var
  ProgId: String;
begin
  if IsAdminInstallMode then
    Result := not RegQueryStringValue(HKLM, HrnKey, '', ProgId)
  else
    Result := not RegQueryStringValue(HKCR, '.hrn', '', ProgId);
  Result := Result or (ProgId = '') or (CompareText(ProgId, SessionProgId) = 0);
end;

function OwnsHrnDefault(): Boolean;
var
  ProgId: String;
begin
  Result := RegQueryStringValue(HKA, HrnKey, '', ProgId) and
            (CompareText(ProgId, SessionProgId) = 0);
end;

function HasRuntime(Root: Integer): Boolean;
var
  Installed, Major, Minor: Cardinal;
begin
  Result := RegQueryDWordValue(Root, RuntimeKey, 'Installed', Installed) and
            (Installed = 1) and
            RegQueryDWordValue(Root, RuntimeKey, 'Major', Major) and
            ((Major > 14) or ((Major = 14) and
             RegQueryDWordValue(Root, RuntimeKey, 'Minor', Minor) and
             (Minor >= {#HeiranVCRuntimeMinor})));
end;

function SameDirectory(const First, Second: String): Boolean;
begin
  Result := (First <> '') and (Second <> '') and
            (CompareText(AddBackslash(ExpandFileName(First)),
                         AddBackslash(ExpandFileName(Second))) = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Destination, Legacy, OtherInstall: String;
  ErrorCode, OtherRoot: Integer;
begin
  Result := '';
  Destination := WizardDirValue;
  Legacy := LegacyInstallDir();
  if IsAdminInstallMode then
    OtherRoot := HKCU
  else
    OtherRoot := HKLM;
  if (RegQueryStringValue(OtherRoot, InstallerKey, 'Inno Setup: App Path', OtherInstall) and
      SameDirectory(Destination, OtherInstall)) or
     (not IsAdminInstallMode and SameDirectory(Destination, Legacy)) then
  begin
    Result := CustomMessage('Heiran_OtherScopeDir');
    Exit;
  end;
  if IsAdminInstallMode and (Legacy <> '') and not SameDirectory(Destination, Legacy) then
  begin
    Result := FmtMessage(CustomMessage('Heiran_LegacyInstallDir'), [Legacy]);
    Exit;
  end;

  if not WizardSilent and not HasRuntime(HKLM64) and not HasRuntime(HKLM32) then
    if MsgBox(FmtMessage(CustomMessage('Heiran_RuntimeMissing'), [IntToStr({#HeiranVCRuntimeMinor})]),
              mbInformation, MB_YESNO) = IDYES then
      ShellExecAsOriginalUser('open', 'https://aka.ms/vs/17/release/vc_redist.x64.exe',
                              '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure RetireLegacyUninstaller();
var
  Directory, Uninstaller, StartMenu, Shortcut, LegacyPath: String;
begin
  if not IsAdminInstallMode then
    Exit;
  Directory := LegacyInstallDir();
  if not SameDirectory(Directory, ExpandConstant('{app}')) then
    Exit;

  Uninstaller := AddBackslash(Directory) + LegacyUninstaller;
  RegQueryStringValue(HKLM32, LegacyKey, 'StartMenu', StartMenu);
  if (FileExists(Uninstaller) and not DeleteFile(Uninstaller)) or
     (RegKeyExists(HKLM32, LegacyKey) and
      not RegDeleteKeyIncludingSubkeys(HKLM32, LegacyKey)) then
  begin
    SuppressibleMsgBox(CustomMessage('Heiran_LegacyCleanupFailed'),
                       mbError, MB_OK, IDOK);
    Exit;
  end;
  if CompareText(StartMenu, 'Heiran') = 0 then
  begin
    Shortcut := ExpandConstant('{commonprograms}\Heiran\Uninstall.lnk');
    DeleteFile(Shortcut);
  end;
  if RegQueryStringValue(HKLM32, 'Software\Heiran\Heiran', '', LegacyPath) and
     SameDirectory(Directory, LegacyPath) then
  begin
    RegDeleteValue(HKLM32, 'Software\Heiran\Heiran', '');
    RegDeleteKeyIfEmpty(HKLM32, 'Software\Heiran\Heiran');
  end;
  RegDeleteKeyIncludingSubkeys(HKLM32, 'Software\Heiran\Capabilities');
  RegDeleteKeyIfEmpty(HKLM32, 'Software\Heiran');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then
    RetireLegacyUninstaller();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and OwnsHrnDefault() then
    RegDeleteValue(HKA, HrnKey, '');
end;
