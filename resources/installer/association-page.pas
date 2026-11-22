type
  TAssociationRow = record
    TaskIndex: Integer;
    Check: TNewCheckBox;
    Toggle: TNewButton;
    Extensions: TNewCheckListBox;
  end;

var
  AssociationPanel: TPanel;
  AssociationHeading: TNewStaticText;
  AssociationRows: array[0..2] of TAssociationRow;
  UpdatingAssociations: Boolean;

procedure SyncAssociationChecks();
var
  I, J, TaskIndex: Integer;
begin
  UpdatingAssociations := True;
  try
    for I := 0 to 2 do begin
      TaskIndex := AssociationRows[I].TaskIndex;
      AssociationRows[I].Check.State := WizardForm.TasksList.State[TaskIndex];
      if AssociationRows[I].Extensions <> nil then
        for J := 0 to AssociationRows[I].Extensions.Items.Count - 1 do
          AssociationRows[I].Extensions.Checked[J] := WizardForm.TasksList.Checked[TaskIndex + J + 1];
    end;
  finally
    UpdatingAssociations := False;
  end;
end;

procedure AssociationCheckClick(Sender: TObject);
var
  I, TaskIndex, ChildIndex: Integer;
begin
  if UpdatingAssociations then
    Exit;
  for I := 0 to 2 do begin
    TaskIndex := AssociationRows[I].TaskIndex;
    if Sender = AssociationRows[I].Check then begin
      if WizardForm.TasksList.State[TaskIndex] = cbChecked then
        WizardForm.TasksList.CheckItem(TaskIndex, coUncheck)
      else
        WizardForm.TasksList.CheckItem(TaskIndex, coCheckWithChildren);
      Break;
    end;
    if Sender = AssociationRows[I].Extensions then begin
      ChildIndex := AssociationRows[I].Extensions.ItemIndex;
      WizardForm.TasksList.Checked[TaskIndex + ChildIndex + 1] :=
        AssociationRows[I].Extensions.Checked[ChildIndex];
      Break;
    end;
  end;
  SyncAssociationChecks();
end;

procedure ArrangeAssociationRows();
var
  I, Y, RowHeight, OpenCount, AvailableHeight, ExtensionHeight: Integer;
begin
  OpenCount := 0;
  for I := 1 to 2 do
    if AssociationRows[I].Extensions.Visible then
      OpenCount := OpenCount + 1;
  RowHeight := ScaleY(28);
  Y := AssociationHeading.Top + AssociationHeading.Height + ScaleY(4);
  AvailableHeight := WizardForm.TasksList.Height - Y - 3 * RowHeight - ScaleY(6);
  for I := 0 to 2 do begin
    AssociationRows[I].Check.Top := Y;
    if AssociationRows[I].Toggle <> nil then
      AssociationRows[I].Toggle.Top := Y + ScaleY(2);
    Y := Y + RowHeight;
    if I > 0 then begin
      if AssociationRows[I].Extensions.Visible then begin
        AssociationRows[I].Toggle.Caption := CustomMessage('Heiran_CollapseExtensions');
        ExtensionHeight := AssociationRows[I].Extensions.Items.Count * ScaleY(18) + ScaleY(4);
        if ExtensionHeight > AvailableHeight div OpenCount then
          ExtensionHeight := AvailableHeight div OpenCount;
        AssociationRows[I].Extensions.Top := Y;
        AssociationRows[I].Extensions.Height := ExtensionHeight;
        Y := Y + ExtensionHeight;
      end else
        AssociationRows[I].Toggle.Caption := CustomMessage('Heiran_ExpandExtensions');
    end;
  end;
  AssociationPanel.Height := Y + ScaleY(6);
end;

procedure AssociationToggleClick(Sender: TObject);
var
  I: Integer;
begin
  for I := 1 to 2 do
    if Sender = AssociationRows[I].Toggle then begin
      AssociationRows[I].Extensions.Visible := not AssociationRows[I].Extensions.Visible;
      Break;
    end;
  ArrangeAssociationRows();
end;

procedure CurPageChanged(CurPageID: Integer);
var
  NativeTasks: TNewCheckListBox;
  I, J, TaskIndex, Inset, ToggleWidth: Integer;
begin
  if (CurPageID <> wpSelectTasks) or WizardSilent then
    Exit;
  NativeTasks := WizardForm.TasksList;
  if AssociationPanel = nil then begin
    { Keep Inno Setup's task control as the source of the installation selections. }
    AssociationRows[0].TaskIndex := NativeTasks.Items.IndexOf('.hrn');
    AssociationRows[1].TaskIndex := NativeTasks.Items.IndexOf(CustomMessage('Heiran_common'));
    AssociationRows[2].TaskIndex := NativeTasks.Items.IndexOf(CustomMessage('Heiran_precision'));
    for I := 0 to 2 do
      if AssociationRows[I].TaskIndex < 0 then
        Exit;

    Inset := ScaleX(8);
    ToggleWidth := ScaleX(115);
    AssociationPanel := TPanel.Create(WizardForm);
    AssociationPanel.Parent := NativeTasks.Parent;
    AssociationPanel.SetBounds(NativeTasks.Left, NativeTasks.Top, NativeTasks.Width, NativeTasks.Height);
    AssociationPanel.Anchors := [akLeft, akTop, akRight];
    AssociationPanel.BevelOuter := bvNone;
    AssociationPanel.BorderStyle := bsSingle;

    AssociationHeading := TNewStaticText.Create(WizardForm);
    AssociationHeading.Parent := AssociationPanel;
    AssociationHeading.SetBounds(Inset, ScaleY(6), AssociationPanel.ClientWidth - 2 * Inset, 0);
    AssociationHeading.Anchors := [akLeft, akTop, akRight];
    AssociationHeading.AutoSize := False;
    AssociationHeading.WordWrap := True;
    AssociationHeading.Caption := CustomMessage('Heiran_AssociationTypes');
    AssociationHeading.AdjustHeight();

    for I := 0 to 2 do begin
      TaskIndex := AssociationRows[I].TaskIndex;
      AssociationRows[I].Check := TNewCheckBox.Create(WizardForm);
      AssociationRows[I].Check.Parent := AssociationPanel;
      AssociationRows[I].Check.SetBounds(Inset, 0,
        AssociationPanel.ClientWidth - 3 * Inset - ToggleWidth, ScaleY(28));
      AssociationRows[I].Check.Anchors := [akLeft, akTop, akRight];
      AssociationRows[I].Check.Caption := NativeTasks.ItemCaption[TaskIndex];
      AssociationRows[I].Check.AllowGrayed := I > 0;
      AssociationRows[I].Check.OnClick := @AssociationCheckClick;
      if I > 0 then begin
        AssociationRows[I].Toggle := TNewButton.Create(WizardForm);
        AssociationRows[I].Toggle.Parent := AssociationPanel;
        AssociationRows[I].Toggle.SetBounds(AssociationPanel.ClientWidth - Inset - ToggleWidth,
          0, ToggleWidth, ScaleY(24));
        AssociationRows[I].Toggle.Anchors := [akTop, akRight];
        AssociationRows[I].Toggle.OnClick := @AssociationToggleClick;

        AssociationRows[I].Extensions := TNewCheckListBox.Create(WizardForm);
        AssociationRows[I].Extensions.Parent := AssociationPanel;
        AssociationRows[I].Extensions.SetBounds(3 * Inset, 0,
          AssociationPanel.ClientWidth - 4 * Inset, 0);
        AssociationRows[I].Extensions.Anchors := [akLeft, akTop, akRight];
        AssociationRows[I].Extensions.BorderStyle := bsSingle;
        AssociationRows[I].Extensions.Visible := False;
        AssociationRows[I].Extensions.OnClickCheck := @AssociationCheckClick;
        J := TaskIndex + 1;
        while J < NativeTasks.Items.Count do begin
          if NativeTasks.ItemLevel[J] <= NativeTasks.ItemLevel[TaskIndex] then
            Break;
          AssociationRows[I].Extensions.AddCheckBox(NativeTasks.ItemCaption[J], '', 0,
            NativeTasks.Checked[J], NativeTasks.ItemEnabled[J], False, True, nil);
          J := J + 1;
        end;
      end;
    end;
    NativeTasks.Visible := False;
  end;
  SyncAssociationChecks();
  ArrangeAssociationRows();
end;
