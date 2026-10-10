#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/HiiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiPopup.h>
#include <MeiMeiDXEv3CoreUnlockProtocol.h>
#include "BC250DXEv3MenuDriver.h"

extern UINT8 BC250DXEv3MenuDriverMenuBin[];
extern UINT8 MeiMeiDXEv3_Menu_DriverStrings[];

EFI_GUID gMenuConfigGuid = { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
EFI_HII_HANDLE mHiiHandle;

//
// Config access protocol installed on the driver handle. The setup browser
// uses it to reach the action buttons' Callback; ExtractConfig/RouteConfig
// bridge the formset's three efivarstores to their EFI variables.
//
EFI_HII_CONFIG_ACCESS_PROTOCOL  mConfigAccess;
EFI_HANDLE                      mDriverHandle;

#define VARIABLE_ATTRIBUTES \
  (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE)

//
// Vendor device path for the handle that carries the config access protocol.
//
typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH  mHiiVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      { (UINT8)(sizeof (VENDOR_DEVICE_PATH)), (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8) }
    },
    // The config variable vendor GUID doubles as the device-path GUID.
    { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

//
// Default SMU governor safe-point curve (frequency MHz / voltage mV). Mirrors
// the reference BC-250 safe-point span; the points through the 2000 MHz
// default maximum are enabled, the higher (aggressive) points are disabled.
//
STATIC CONST SMU_SAFE_POINT  mDefaultSmuSafePoints[SMU_SAFE_POINT_COUNT] = {
  { 1, 0,  400,  700 },
  { 1, 0, 1175,  700 },
  { 1, 0, 1400,  750 },
  { 1, 0, 1600,  800 },
  { 1, 0, 1700,  850 },
  { 1, 0, 1850,  900 },
  { 1, 0, 2000,  950 },
  { 0, 0, 2050,  975 },
  { 0, 0, 2100, 1000 },
  { 0, 0, 2125, 1015 },
  { 0, 0, 2150, 1025 },
  { 0, 0, 2200, 1045 },
  { 0, 0, 2230, 1075 },
  { 0, 0, 2300, 1100 },
  { 0, 0, 2350, 1125 },
};

#define SMU_GOVERNOR_MIN_FREQ_DEFAULT  400
#define SMU_GOVERNOR_MAX_FREQ_DEFAULT  2000

//
// Forward declarations for the config access protocol implementation.
//
EFI_STATUS
EFIAPI
ExtractConfig (
  IN CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST EFI_STRING                      Request,
  OUT EFI_STRING                           *Progress,
  OUT EFI_STRING                           *Results
  );

EFI_STATUS
EFIAPI
RouteConfig (
  IN CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST EFI_STRING                      Configuration,
  OUT EFI_STRING                           *Progress
  );

EFI_STATUS
EFIAPI
Callback (
  IN     CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN     EFI_BROWSER_ACTION                    Action,
  IN     EFI_QUESTION_ID                       QuestionId,
  IN     UINT8                                 Type,
  IN OUT EFI_IFR_TYPE_VALUE                    *Value,
  OUT    EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  );


//
// Initialize a single feature variable if it does not already exist.
// Returns EFI_SUCCESS on success (or if the variable already existed).
//
EFI_STATUS
EFIAPI
InitVariableIfMissing (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *Guid,
  IN VOID      *Config,
  IN UINTN     ConfigSize
  )
{
  EFI_STATUS  Status;
  UINTN       Size = ConfigSize;

  Status = gRT->GetVariable (VariableName, Guid, NULL, &Size, Config);

  if (Status == EFI_NOT_FOUND) {
    Status = gRT->SetVariable (
                VariableName,
                Guid,
                VARIABLE_ATTRIBUTES,
                ConfigSize,
                Config
                );

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

/**
  Display a modal prompt directly on the console.

  AMI's proprietary setup browser does not produce the edk2
  EFI_HII_POPUP_PROTOCOL, so popups are drawn manually on gST->ConOut and the
  selection is read from gST->ConIn. This works in any setup environment
  because the browser shares the same console.

  @param[in]  Style      Popup style (info / warning / error); only the
                         color/attribute selection is affected.
  @param[in]  PopupType  Popup type (Ok, YesNo, ...). The box is modal and
                         waits for the user selection.
  @param[in]  Message    String token for the message (resolved against
                         mHiiHandle).

  @return  The user selection, or EfiHiiPopupSelectionCancel when the popup
           could not be shown.
**/
STATIC
EFI_HII_POPUP_SELECTION
ShowPopup (
  IN EFI_HII_POPUP_STYLE  Style,
  IN EFI_HII_POPUP_TYPE   PopupType,
  IN EFI_STRING_ID        Message
  )
{
  EFI_HII_POPUP_SELECTION         Selection;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
  EFI_SIMPLE_TEXT_OUTPUT_MODE     SavedConsoleMode;
  EFI_INPUT_KEY                   Key;
  CHAR16                          *Text;
  CHAR16                          *Start;
  CHAR16                          *Line;
  CHAR16                          *Cursor;
  UINTN                           Columns;
  UINTN                           Rows;
  UINTN                           MaxLength;
  UINTN                           NumberOfLines;
  UINTN                           Row;
  UINTN                           Column;
  UINTN                           Index;
  UINTN                           Length;
  UINTN                           Attribute;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *BltBuffer;
  UINTN                           BoxLeft;
  UINTN                           BoxTop;
  UINTN                           BoxWidth;
  UINTN                           BoxHeight;
  UINTN                           PixelLeft;
  UINTN                           PixelTop;
  UINTN                           PixelWidth;
  UINTN                           PixelHeight;
  UINTN                           PixelCellWidth;
  UINTN                           PixelCellHeight;
  EFI_STATUS                      Status;

  Selection       = EfiHiiPopupSelectionCancel;
  ConOut          = gST->ConOut;
  ConIn           = gST->ConIn;
  Text            = NULL;
  Start           = NULL;
  Line            = NULL;
  Gop             = NULL;
  BltBuffer       = NULL;

  if ((Style == EfiHiiPopupStyleError) || (Style == EfiHiiPopupStyleWarning)) {
    Attribute = EFI_WHITE | EFI_BACKGROUND_RED;
  } else {
    Attribute = EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE;
  }

  Text = HiiGetString (mHiiHandle, Message, NULL);
  if ((Text == NULL) || (ConOut == NULL)) {
    if (Text != NULL) {
      FreePool (Text);
    }

    return EfiHiiPopupSelectionCancel;
  }

  Start = Text;

  //
  // Drain any queued keystrokes (for example the key press that activated
  // the action button, or a repeating key) so a stale key cannot dismiss
  // the dialog before it is visible.
  //
  if (ConIn != NULL) {
    while (!EFI_ERROR (ConIn->ReadKeyStroke (ConIn, &Key))) {
    }
  }

  //
  // Split the message into lines and compute the box dimensions. Lines are
  // separated by '\n'; a trailing '\r' is treated as part of the break.
  //
  MaxLength     = 0;
  NumberOfLines = 0;
  do {
    Cursor = Text;
    while ((*Cursor != L'\n') && (*Cursor != L'\0')) {
      Cursor++;
    }

    Length = (UINTN)(Cursor - Text);
    if ((Length > 0) && (Text[Length - 1] == L'\r')) {
      Length--;
    }

    MaxLength = MAX (MaxLength, Length + 2);
    NumberOfLines++;
    if (*Cursor == L'\0') {
      break;
    }

    Text = Cursor + 1;
  } while (TRUE);

  //
  // Bound the box to the visible console and center it. The box is
  // NumberOfLines message rows plus top, empty, hint, and bottom rows.
  //
  Columns = 80;
  Rows    = 25;
  ConOut->QueryMode (ConOut, ConOut->Mode->Mode, &Columns, &Rows);
  MaxLength     = MIN (MaxLength, Columns - 4);
  NumberOfLines = MIN (NumberOfLines, Rows - 6);

  Column = (Columns - (MaxLength + 2)) / 2;
  Row    = (Rows - (NumberOfLines + 4)) / 2;

  //
  // Remember the box footprint for the screen restore after dismissal.
  //
  BoxLeft   = Column;
  BoxTop    = Row;
  BoxWidth  = MaxLength + 2;
  BoxHeight = NumberOfLines + 4;

  if (ConIn == NULL) {
    FreePool (Text);
    return EfiHiiPopupSelectionCancel;
  }

  Text = Start;

  Line = AllocateZeroPool ((MaxLength + 3) * sizeof (CHAR16));
  if (Line == NULL) {
    FreePool (Text);
    return EfiHiiPopupSelectionCancel;
  }

  //
  // Snapshot the console region behind the popup through GOP->Blt (which
  // handles the pixel format and framebuffer mapping) so the exact glyphs
  // and colors can be restored after dismissal. The setup browser does not
  // repaint this region because the box is not part of its display model.
  // One extra character cell is captured on every side so glyph overflow
  // (text cells are often taller/wider than the integer division result)
  // at the box edges is covered. The margin contains pre-popup pixels and
  // is restored unchanged.
  //
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && (Gop != NULL) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL) &&
      (Columns != 0) && (Rows != 0))
  {
    PixelCellWidth  = Gop->Mode->Info->HorizontalResolution / Columns;
    PixelCellHeight = Gop->Mode->Info->VerticalResolution / Rows;

    PixelLeft   = BoxLeft * PixelCellWidth;
    PixelTop    = BoxTop * PixelCellHeight;
    PixelWidth  = BoxWidth * PixelCellWidth;
    PixelHeight = BoxHeight * PixelCellHeight;

    //
    // Expand the capture by one cell on each side (clamped to the display)
    // so any box pixels drawn outside the exact box rectangle, such as the
    // tall-glyph slack beneath the bottom border row, are inside the region
    // that is restored after dismissal.
    //
    PixelLeft  = (PixelLeft > PixelCellWidth) ? (PixelLeft - PixelCellWidth) : 0;
    PixelTop   = (PixelTop > PixelCellHeight) ? (PixelTop - PixelCellHeight) : 0;
    PixelWidth  += 2 * PixelCellWidth;
    PixelHeight += 2 * PixelCellHeight;

    if (PixelLeft + PixelWidth > Gop->Mode->Info->HorizontalResolution) {
      PixelWidth = Gop->Mode->Info->HorizontalResolution - PixelLeft;
    }

    if (PixelTop + PixelHeight > Gop->Mode->Info->VerticalResolution) {
      PixelHeight = Gop->Mode->Info->VerticalResolution - PixelTop;
    }

    if ((PixelWidth > 0) && (PixelHeight > 0)) {
      BltBuffer = AllocatePool (PixelWidth * PixelHeight * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      if (BltBuffer != NULL) {
        Status = Gop->Blt (
                       Gop,
                       BltBuffer,
                       EfiBltVideoToBltBuffer,
                       PixelLeft,
                       PixelTop,
                       0,
                       0,
                       PixelWidth,
                       PixelHeight,
                       0
                       );
        if (EFI_ERROR (Status)) {
          FreePool (BltBuffer);
          BltBuffer = NULL;
        }
      }
    }
  }

  //
  // Save the console state and draw the box with the requested style.
  //
  CopyMem (&SavedConsoleMode, ConOut->Mode, sizeof (SavedConsoleMode));
  ConOut->EnableCursor (ConOut, FALSE);
  ConOut->SetAttribute (ConOut, Attribute);

  SetMem16 (Line, (MaxLength + 2) * sizeof (CHAR16), BOXDRAW_HORIZONTAL);
  Line[0]             = BOXDRAW_DOWN_RIGHT;
  Line[MaxLength + 1] = BOXDRAW_DOWN_LEFT;
  Line[MaxLength + 2] = L'\0';
  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  //
  // Draw the message lines.
  //
  do {
    Cursor = Text;
    while ((*Cursor != L'\n') && (*Cursor != L'\0')) {
      Cursor++;
    }

    Length = (UINTN)(Cursor - Text);
    if ((Length > 0) && (Text[Length - 1] == L'\r')) {
      Length--;
    }

    if (Length > MaxLength) {
      Length = MaxLength;
    }

    SetMem16 (Line, (MaxLength + 2) * sizeof (CHAR16), L' ');
    Line[0]             = BOXDRAW_VERTICAL;
    Line[MaxLength + 1] = BOXDRAW_VERTICAL;
    Line[MaxLength + 2] = L'\0';
    if (Length > 0) {
      CopyMem (&Line[1], Text, Length * sizeof (CHAR16));
    }

    ConOut->SetCursorPosition (ConOut, Column, Row++);
    ConOut->OutputString (ConOut, Line);

    NumberOfLines--;
    if ((*Cursor == L'\0') || (NumberOfLines == 0)) {
      break;
    }

    Text = Cursor + 1;
  } while (TRUE);

  //
  // Draw an empty line and the selectable-option hint.
  //
  SetMem16 (Line, (MaxLength + 2) * sizeof (CHAR16), L' ');
  Line[0]             = BOXDRAW_VERTICAL;
  Line[MaxLength + 1] = BOXDRAW_VERTICAL;
  Line[MaxLength + 2] = L'\0';
  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  if (PopupType == EfiHiiPopupTypeYesNo) {
    Length = StrLen (L"[Y]es  [N]o");
    if (Length > MaxLength) {
      Length = MaxLength;
    }

    CopyMem (&Line[(MaxLength - Length) / 2 + 1], L"[Y]es  [N]o", Length * sizeof (CHAR16));
  } else {
    Length = StrLen (L"<Enter to continue>");
    if (Length > MaxLength) {
      Length = MaxLength;
    }

    CopyMem (&Line[(MaxLength - Length) / 2 + 1], L"<Enter to continue>", Length * sizeof (CHAR16));
  }

  Line[MaxLength + 2] = L'\0';
  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  SetMem16 (Line, (MaxLength + 2) * sizeof (CHAR16), BOXDRAW_HORIZONTAL);
  Line[0]             = BOXDRAW_UP_RIGHT;
  Line[MaxLength + 1] = BOXDRAW_UP_LEFT;
  Line[MaxLength + 2] = L'\0';
  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  //
  // Restore the console state before waiting for input.
  //
  ConOut->EnableCursor (ConOut, SavedConsoleMode.CursorVisible);
  ConOut->SetCursorPosition (ConOut, SavedConsoleMode.CursorColumn, SavedConsoleMode.CursorRow);
  ConOut->SetAttribute (ConOut, SavedConsoleMode.Attribute);

  Selection = EfiHiiPopupSelectionCancel;
  for (;;) {
    Status = ConIn->ReadKeyStroke (ConIn, &Key);
    if (EFI_ERROR (Status)) {
      if (Status == EFI_NOT_READY) {
        gBS->WaitForEvent (1, &ConIn->WaitForKey, &Index);
      }

      continue;
    }

    if (PopupType == EfiHiiPopupTypeOk) {
      Selection = EfiHiiPopupSelectionOk;
      break;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
        (Key.UnicodeChar == L'y') || (Key.UnicodeChar == L'Y'))
    {
      Selection = EfiHiiPopupSelectionYes;
      break;
    } else if ((Key.UnicodeChar == L'n') || (Key.UnicodeChar == L'N') ||
               (Key.ScanCode == SCAN_ESC))
    {
      Selection = EfiHiiPopupSelectionNo;
      break;
    }
  }

  //
  // Restore the screen. If the framebuffer snapshot succeeded, blit it back
  // so the popup disappears exactly as it was drawn and the user stays inside
  // the submenu. Otherwise blank the box footprint as a fallback (the browser
  // does not repaint the region on its own).
  //
  if (BltBuffer != NULL) {
    Status = Gop->Blt (
                   Gop,
                   BltBuffer,
                   EfiBltBufferToVideo,
                   0,
                   0,
                   PixelLeft,
                   PixelTop,
                   PixelWidth,
                   PixelHeight,
                   0
                   );
    FreePool (BltBuffer);
    BltBuffer = NULL;
  } else {
    Status = EFI_UNSUPPORTED;
  }

  if (EFI_ERROR (Status)) {
    BoxHeight = Row - BoxTop;
    SetMem16 (Line, BoxWidth * sizeof (CHAR16), L' ');
    Line[BoxWidth] = L'\0';
    ConOut->SetAttribute (ConOut, SavedConsoleMode.Attribute);
    for (Index = 0; Index < BoxHeight; Index++) {
      ConOut->SetCursorPosition (ConOut, BoxLeft, BoxTop + Index);
      ConOut->OutputString (ConOut, Line);
    }
  }

  ConOut->SetCursorPosition (ConOut, SavedConsoleMode.CursorColumn, SavedConsoleMode.CursorRow);

  FreePool (Line);
  FreePool (Text);

  return Selection;
}

/**
  Reset the currently selected per-core toggles to the learned factory core
  mask, obtained through the MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL.

  @retval EFI_STATUS  Result of the reset operation. Conditions that should be
                      reported to the user (core-unlock driver missing, factory
                      mask not yet learned) are shown as popups and return
                      EFI_SUCCESS.
**/
STATIC
EFI_STATUS
ResetToFactoryMask (VOID)
{
  EFI_STATUS                       Status;
  MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL *Protocol;
  UINT8                            FactoryMask;
  UINTN                            Index;
  CORE_CONFIG                      CoreConfig;

  //
  // The factory mask is owned by the BC250-DXEv3 Core Unlock driver. Keep the
  // menu usable when that driver is not present.
  //
  Status = gBS->LocateProtocol (&gMeiMeiDXEv3CoreUnlockProtocolGuid, NULL, (VOID **)&Protocol);
  if (EFI_ERROR (Status) || (Protocol == NULL) || (Protocol->GetFactoryMask == NULL)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3MenuDriver: core unlock protocol unavailable (Status=%r, Protocol=%p)\n", Status, Protocol));
    ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeOk, STRING_TOKEN (STR_RESTORE_NO_PROTOCOL_TEXT));
    return EFI_SUCCESS;
  }

  Status = Protocol->GetFactoryMask (Protocol, &FactoryMask);
  if (EFI_ERROR (Status)) {
    //
    // EFI_NOT_FOUND: the one-shot cold-boot learning cycle has not completed.
    //
    DEBUG ((DEBUG_ERROR, "BC250DXEv3MenuDriver: GetFactoryMask failed: %r\n", Status));
    ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeOk, STRING_TOKEN (STR_RESTORE_NO_FACTORY_MASK_TEXT));
    return EFI_SUCCESS;
  }

  //
  // Build the per-core toggles from the factory mask bits.
  //
  ZeroMem (&CoreConfig, sizeof (CORE_CONFIG));
  CoreConfig.CoreUnlock = CORE_UNLOCK_CUSTOM;
  for (Index = 0; Index < 8; Index++) {
    ((UINT8 *)&CoreConfig.Core0)[Index] = (UINT8)((FactoryMask >> Index) & 1U);
  }

  //
  // Persist immediately (the efivarstore questions write the variable as they
  // change) and keep the browser's copy of the varstore in sync.
  //
  Status = gRT->SetVariable (
                  L"MeiMeiDXEv3CoreVar",
                  &gMenuConfigGuid,
                  VARIABLE_ATTRIBUTES,
                  sizeof (CORE_CONFIG),
                  &CoreConfig
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3MenuDriver: failed to reset core config: %r\n", Status));
    return Status;
  }

  HiiSetBrowserData (
    &gMenuConfigGuid,
    L"MeiMeiDXEv3CoreVar",
    sizeof (CORE_CONFIG),
    (UINT8 *)&CoreConfig,
    NULL
    );

  DEBUG ((DEBUG_INFO, "BC250DXEv3MenuDriver: restored core mask to factory mask 0x%02X\n", FactoryMask));

  return EFI_SUCCESS;
}

/**
  Config access callback. Handles the "Restore Factory Core Mask" action
  button in the Individual Core Options submenu.

  Restore asks for a Yes/No confirmation and, on Yes, resets the selected
  toggles to the learned factory core mask. The valid/invalid core-mask status
  is shown declaratively in the form with suppressif-gated text, so it needs
  no callback handling here.

  The work is performed on EFI_BROWSER_ACTION_CHANGED (after the browser has
  finished its CHANGING phase and consumed the activating keystroke), so the
  native popups render reliably.

  @param  This          Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param  Action        Type of action taken by the browser.
  @param  QuestionId    ID of the question the action is taken on.
  @param  Type          Type of the question value.
  @param  Value         Value of the question.
  @param  ActionRequest On return, points to the action requested by the user.

  @retval EFI_SUCCESS       The callback handled the action.
  @retval EFI_UNSUPPORTED   The callback does not handle this action/question;
                            the setup browser treats this as success.
**/
EFI_STATUS
EFIAPI
Callback (
  IN     CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN     EFI_BROWSER_ACTION                    Action,
  IN     EFI_QUESTION_ID                       QuestionId,
  IN     UINT8                                 Type,
  IN OUT EFI_IFR_TYPE_VALUE                    *Value,
  OUT    EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  EFI_STATUS  Status;

  if (ActionRequest == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *ActionRequest = EFI_BROWSER_ACTION_REQUEST_NONE;

  DEBUG ((DEBUG_INFO, "BC250DXEv3MenuDriver: Callback(Action=0x%X, QuestionId=0x%X)\n", Action, QuestionId));

  //
  // CHANGING is dispatched while the browser is still processing the key
  // press that selected the button; a popup from that phase would draw over
  // the previous frame and could be dismissed before it is ever visible.
  // Accept the change and let the browser finish its CHANGING phase so it
  // dispatches CHANGED, where the popup and reset work is performed.
  //
  if (Action == EFI_BROWSER_ACTION_CHANGING) {
    return EFI_SUCCESS;
  }

  if (Action != EFI_BROWSER_ACTION_CHANGED) {
    return EFI_UNSUPPORTED;
  }

  //
  // Restore action: ask for confirmation, then reset the selected toggles to
  // the learned factory core mask. ShowPopup restores the console region
  // itself, so the user stays inside the submenu. After the reset, request a
  // form apply so the browser refreshes the changed checkbox values (best
  // effort; the varstore and the browser data are already updated either way).
  //
  if (QuestionId == CORES_RESTORE_FACTORY_QUESTION_ID) {
    if (ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeYesNo, STRING_TOKEN (STR_RESTORE_FACTORY_CONFIRM_TEXT)) ==
        EfiHiiPopupSelectionYes)
    {
      Status = ResetToFactoryMask ();
      *ActionRequest = EFI_BROWSER_ACTION_REQUEST_FORM_APPLY;
    } else {
      Status = EFI_SUCCESS;
    }

    return Status;
  }

  return EFI_UNSUPPORTED;
}

/**
  Extract the current settings for the formset's varstores.

  Bridges each varstore to its EFI variable: the request is matched against
  the three varstores by their config header, the variable is read, and the
  value is converted to a <ConfigResp> string. This preserves the behavior the
  browser had before this protocol was installed.

  @param  This      Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param  Request   A null-terminated Unicode string in <ConfigRequest> format.
  @param  Progress  On return, points to a character in the Request string.
  @param  Results   A null-terminated Unicode string in <ConfigResp> format.

  @retval EFI_SUCCESS            The configuration was returned.
  @retval EFI_NOT_FOUND          Routing data does not match any varstore.
  @retval EFI_OUT_OF_RESOURCES   Not enough memory.
  @retval other                  The variable could not be converted.
**/
EFI_STATUS
EFIAPI
ExtractConfig (
  IN CONST  EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST  EFI_STRING                      Request,
  OUT       EFI_STRING                      *Progress,
  OUT       EFI_STRING                      *Results
  )
{
  EFI_STATUS  Status;
  EFI_STRING  ConfigRequest;
  EFI_STRING  ConfigHdr;
  CHAR16      *VarName;
  UINTN       BufferSize;
  UINT8       *Buffer;
  UINTN       Size;
  BOOLEAN     AllocatedRequest;

  if ((Progress == NULL) || (Results == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress        = Request;
  ConfigRequest    = NULL;
  ConfigHdr        = NULL;
  AllocatedRequest = FALSE;

  if (Request == NULL) {
    //
    // No request: default to the core varstore.
    //
    VarName    = L"MeiMeiDXEv3CoreVar";
    BufferSize = sizeof (CORE_CONFIG);

    ConfigHdr = HiiConstructConfigHdr (&gMenuConfigGuid, VarName, mDriverHandle);
    if (ConfigHdr == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Size          = (StrLen (ConfigHdr) + 32 + 1) * sizeof (CHAR16);
    ConfigRequest = AllocateZeroPool (Size);
    if (ConfigRequest == NULL) {
      FreePool (ConfigHdr);
      return EFI_OUT_OF_RESOURCES;
    }

    UnicodeSPrint (ConfigRequest, Size, L"%s&OFFSET=0&WIDTH=%016LX", ConfigHdr, (UINT64)BufferSize);
    FreePool (ConfigHdr);
    AllocatedRequest = TRUE;
  } else {
    //
    // Match the request against the three varstores of this formset.
    //
    if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, L"MeiMeiDXEv3AcpiVar")) {
      VarName    = L"MeiMeiDXEv3AcpiVar";
      BufferSize = sizeof (ACPI_CONFIG);
    } else if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, L"MeiMeiDXEv3SmuUnlockVar")) {
      VarName    = L"MeiMeiDXEv3SmuUnlockVar";
      BufferSize = sizeof (SMU_UNLOCK_CONFIG);
    } else if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, L"MeiMeiDXEv3SmuPatchVar")) {
      VarName    = L"MeiMeiDXEv3SmuPatchVar";
      BufferSize = sizeof (SMU_PATCH_CONFIG);
    } else if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, SMU_GOVERNOR_ENABLE_VAR_NAME)) {
      VarName    = SMU_GOVERNOR_ENABLE_VAR_NAME;
      BufferSize = sizeof (SMU_GOVERNOR_ENABLE_CONFIG);
    } else if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, SMU_GOVERNOR_CONFIG_VAR_NAME)) {
      VarName    = SMU_GOVERNOR_CONFIG_VAR_NAME;
      BufferSize = sizeof (SMU_GOVERNOR_CONFIG);
    } else if (HiiIsConfigHdrMatch (Request, &gMenuConfigGuid, L"MeiMeiDXEv3CoreVar")) {
      VarName    = L"MeiMeiDXEv3CoreVar";
      BufferSize = sizeof (CORE_CONFIG);
    } else {
      return EFI_NOT_FOUND;
    }

    ConfigRequest = Request;
  }

  if (gHiiConfigRouting == NULL) {
    if (AllocatedRequest) {
      FreePool (ConfigRequest);
    }
    return EFI_UNSUPPORTED;
  }

  Buffer = AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    if (AllocatedRequest) {
      FreePool (ConfigRequest);
    }
    return EFI_OUT_OF_RESOURCES;
  }

  Size = BufferSize;
  Status = gRT->GetVariable (VarName, &gMenuConfigGuid, NULL, &Size, Buffer);
  if (EFI_ERROR (Status)) {
    //
    // Absent or unexpected size: reply with the defaults (zeros).
    //
    ZeroMem (Buffer, BufferSize);
  }

  Status = gHiiConfigRouting->BlockToConfig (
                                gHiiConfigRouting,
                                ConfigRequest,
                                Buffer,
                                BufferSize,
                                Results,
                                Progress
                                );

  FreePool (Buffer);
  if (AllocatedRequest) {
    FreePool (ConfigRequest);
  }

  return Status;
}

/**
  Route the submitted configuration to the backing EFI variables.

  Parses the <ConfigResp>, writes the varstore variable, and keeps the
  browser's copy in sync.

  @param  This           Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param  Configuration  A null-terminated Unicode string in <ConfigResp> format.
  @param  Progress       On return, points to a character in the Configuration
                         string.

  @retval EFI_SUCCESS       The configuration was processed.
  @retval EFI_NOT_FOUND     Routing data does not match any varstore.
  @retval EFI_UNSUPPORTED   The config routing protocol is unavailable.
  @retval other             The configuration could not be parsed or stored.
**/
EFI_STATUS
EFIAPI
RouteConfig (
  IN CONST  EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST  EFI_STRING                      Configuration,
  OUT       EFI_STRING                      *Progress
  )
{
  EFI_STATUS  Status;
  CHAR16      *VarName;
  UINTN       BufferSize;
  UINT8       *Buffer;
  UINTN       BlockSize;
  UINTN       Size;

  if ((Configuration == NULL) || (Progress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Configuration;

  if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, L"MeiMeiDXEv3AcpiVar")) {
    VarName    = L"MeiMeiDXEv3AcpiVar";
    BufferSize = sizeof (ACPI_CONFIG);
  } else if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, L"MeiMeiDXEv3SmuUnlockVar")) {
    VarName    = L"MeiMeiDXEv3SmuUnlockVar";
    BufferSize = sizeof (SMU_UNLOCK_CONFIG);
  } else if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, L"MeiMeiDXEv3SmuPatchVar")) {
    VarName    = L"MeiMeiDXEv3SmuPatchVar";
    BufferSize = sizeof (SMU_PATCH_CONFIG);
  } else if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, SMU_GOVERNOR_ENABLE_VAR_NAME)) {
    VarName    = SMU_GOVERNOR_ENABLE_VAR_NAME;
    BufferSize = sizeof (SMU_GOVERNOR_ENABLE_CONFIG);
  } else if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, SMU_GOVERNOR_CONFIG_VAR_NAME)) {
    VarName    = SMU_GOVERNOR_CONFIG_VAR_NAME;
    BufferSize = sizeof (SMU_GOVERNOR_CONFIG);
  } else if (HiiIsConfigHdrMatch (Configuration, &gMenuConfigGuid, L"MeiMeiDXEv3CoreVar")) {
    VarName    = L"MeiMeiDXEv3CoreVar";
    BufferSize = sizeof (CORE_CONFIG);
  } else {
    return EFI_NOT_FOUND;
  }

  if (gHiiConfigRouting == NULL) {
    return EFI_UNSUPPORTED;
  }

  Buffer = AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Pre-fill from the variable so partial configs update in place.
  //
  Size = BufferSize;
  Status = gRT->GetVariable (VarName, &gMenuConfigGuid, NULL, &Size, Buffer);
  if (EFI_ERROR (Status)) {
    ZeroMem (Buffer, BufferSize);
  }

  BlockSize = BufferSize;
  Status = gHiiConfigRouting->ConfigToBlock (
                                gHiiConfigRouting,
                                Configuration,
                                Buffer,
                                &BlockSize,
                                Progress
                                );
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  Status = gRT->SetVariable (
                  VarName,
                  &gMenuConfigGuid,
                  VARIABLE_ATTRIBUTES,
                  BlockSize,
                  Buffer
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  //
  // Keep the browser's uncommitted state in sync.
  //
  HiiSetBrowserData (&gMenuConfigGuid, VarName, BlockSize, Buffer, NULL);

  FreePool (Buffer);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Bc250DxeV3MenuDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS   Status;
  ACPI_CONFIG         AcpiConfig;
  SMU_UNLOCK_CONFIG   SmuUnlockConfig;
  SMU_PATCH_CONFIG    SmuPatchConfig;
  SMU_GOVERNOR_ENABLE_CONFIG SmuGovernorEnableConfig;
  SMU_GOVERNOR_CONFIG SmuGovernorConfig;
  CORE_CONFIG         CoreConfig;
  UINTN        Index;
  EFI_GUID     HiiGuid = gEfiCallerIdGuid;

  // Workaround to avoid problems with LTO in the compiler
  volatile UINT8 LtoBypass;
  LtoBypass = BC250DXEv3MenuDriverMenuBin[0] + MeiMeiDXEv3_Menu_DriverStrings[0];
  (VOID)LtoBypass;

  // Initialize the ACPI configuration variable if it does not exist
  AcpiConfig.AcpiPatch = 0;
  Status = InitVariableIfMissing (L"MeiMeiDXEv3AcpiVar", &gMenuConfigGuid, &AcpiConfig, sizeof (ACPI_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  // Initialize the SMU unlock configuration variable if it does not exist
  SmuUnlockConfig.SmuUnlock = 0;
  Status = InitVariableIfMissing (L"MeiMeiDXEv3SmuUnlockVar", &gMenuConfigGuid, &SmuUnlockConfig, sizeof (SMU_UNLOCK_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  // Initialize the SMU patch configuration variable if it does not exist
  SmuPatchConfig.SmuReportingPatch = 0;
  Status = InitVariableIfMissing (L"MeiMeiDXEv3SmuPatchVar", &gMenuConfigGuid, &SmuPatchConfig, sizeof (SMU_PATCH_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  // Initialize the Core configuration variable if it does not exist
  CoreConfig.CoreUnlock = CORE_UNLOCK_DISABLED;
  CoreConfig.Core0 = 0;
  CoreConfig.Core1 = 0;
  CoreConfig.Core2 = 0;
  CoreConfig.Core3 = 0;
  CoreConfig.Core4 = 0;
  CoreConfig.Core5 = 0;
  CoreConfig.Core6 = 0;
  CoreConfig.Core7 = 0;
  Status = InitVariableIfMissing (L"MeiMeiDXEv3CoreVar", &gMenuConfigGuid, &CoreConfig, sizeof (CORE_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  // Initialize the SMU governor enable variable if it does not exist
  // (fail-closed: disabled by default).
  SmuGovernorEnableConfig.SmuGovernor = 0;
  Status = InitVariableIfMissing (SMU_GOVERNOR_ENABLE_VAR_NAME, &gMenuConfigGuid, &SmuGovernorEnableConfig, sizeof (SMU_GOVERNOR_ENABLE_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  // Initialize the SMU governor configuration variable if it does not exist.
  ZeroMem (&SmuGovernorConfig, sizeof (SMU_GOVERNOR_CONFIG));
  SmuGovernorConfig.Schema       = SMU_SAFE_POINTS_SCHEMA;
  SmuGovernorConfig.Count        = SMU_SAFE_POINT_COUNT;
  SmuGovernorConfig.MinFrequency = SMU_GOVERNOR_MIN_FREQ_DEFAULT;
  SmuGovernorConfig.MaxFrequency = SMU_GOVERNOR_MAX_FREQ_DEFAULT;
  for (Index = 0; Index < SMU_SAFE_POINT_COUNT; Index++) {
    SmuGovernorConfig.Point[Index] = mDefaultSmuSafePoints[Index];
  }
  Status = InitVariableIfMissing (SMU_GOVERNOR_CONFIG_VAR_NAME, &gMenuConfigGuid, &SmuGovernorConfig, sizeof (SMU_GOVERNOR_CONFIG));
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    return Status;
  }

  //
  // Install the config access protocol (with our Callback) on a driver handle
  // and register the formset against it, so the setup browser can reach the
  // action buttons' Callback and the varstore bridge.
  //
  mConfigAccess.ExtractConfig = ExtractConfig;
  mConfigAccess.RouteConfig   = RouteConfig;
  mConfigAccess.Callback      = Callback;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mDriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mHiiVendorDevicePath,
                  &gEfiHiiConfigAccessProtocolGuid,
                  &mConfigAccess,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register our form data with HII
  mHiiHandle = HiiAddPackages (
                 &HiiGuid,
                 mDriverHandle,
                 MeiMeiDXEv3_Menu_DriverStrings,
                 BC250DXEv3MenuDriverMenuBin,
                 NULL
                 );

  if (mHiiHandle == NULL) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}