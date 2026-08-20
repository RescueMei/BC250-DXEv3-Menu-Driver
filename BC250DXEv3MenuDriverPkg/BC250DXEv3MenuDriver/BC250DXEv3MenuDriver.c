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
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiPopup.h>
#include <MeiMeiDXEv3CoreUnlockProtocol.h>
#include "BC250DXEv3MenuDriver.h"

extern UINT8 BC250DXEv3MenuDriverMenuBin[];
extern UINT8 MeiMeiDXEv3MenuDriverStrings[];

EFI_GUID gMenuConfigGuid = { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
EFI_HII_HANDLE mHiiHandle;

//
// Config access protocol installed on the driver handle. The setup browser
// uses it to reach the validate action's Callback; ExtractConfig/RouteConfig
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
  Count the number of enabled cores in a core presence mask byte.

  @param[in]  Mask  Core presence mask low byte.

  @return  Number of set bits (0..8).
**/
STATIC
UINTN
CountCoreBits (
  IN UINT8  Mask
  )
{
  UINTN  Count;
  UINTN  Bit;

  Count = 0;
  for (Bit = 0; Bit < 8; Bit++) {
    if ((Mask & (UINT8)(1U << Bit)) != 0) {
      Count++;
    }
  }

  return Count;
}

/**
  Assemble the custom core presence mask low byte from the per-core toggles.

  @param[in]  Config  Core configuration.

  @return  Core presence mask low byte built from Core0..Core7.
**/
STATIC
UINT8
ComputeCustomMask (
  IN CONST CORE_CONFIG  *Config
  )
{
  return (UINT8)((Config->Core0      ) |
                 (Config->Core1 << 1 ) |
                 (Config->Core2 << 2 ) |
                 (Config->Core3 << 3 ) |
                 (Config->Core4 << 4 ) |
                 (Config->Core5 << 5 ) |
                 (Config->Core6 << 6 ) |
                 (Config->Core7 << 7 ));
}

/**
  Determine whether a custom core presence mask is a valid CCX0/CCX1
  configuration. Mirrors the rule set enforced by the BC250-DXEv3 SMU Core
  Unlock driver:
    * CCX0 (cores 0-3) must have at least one core enabled.
    * CCX1 (cores 4-7) must have the same number of cores enabled as CCX0,
      or have no cores enabled at all.

  @param[in]  Mask  Core presence mask low byte.

  @retval TRUE   The mask is a valid CCX0/CCX1 configuration.
  @retval FALSE  The mask violates the CCX0/CCX1 rules.
**/
STATIC
BOOLEAN
IsCoreMaskValid (
  IN UINT8  Mask
  )
{
  UINTN  Ccx0Count;
  UINTN  Ccx1Count;

  Ccx0Count = CountCoreBits ((UINT8)(Mask & BC250_CCX0_CORE_BITS));
  Ccx1Count = CountCoreBits ((UINT8)(Mask & BC250_CCX1_CORE_BITS));

  if (Ccx0Count == 0) {
    return FALSE;
  }

  if ((Ccx1Count != 0) && (Ccx1Count != Ccx0Count)) {
    return FALSE;
  }

  return TRUE;
}

/**
  Read the current core configuration from MeiMeiDXEv3CoreVar.

  The efivarstore questions in this form write straight to the variable when
  changed in the setup browser, so this reflects the current on-screen
  selection.

  @param[out] Config  Receives the core configuration (defaults when absent).

  @retval EFI_SUCCESS  The configuration was read or defaulted.
**/
STATIC
EFI_STATUS
ReadCoreConfig (
  OUT CORE_CONFIG  *Config
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  Size   = sizeof (CORE_CONFIG);
  Status = gRT->GetVariable (
                  L"MeiMeiDXEv3CoreVar",
                  &gMenuConfigGuid,
                  NULL,
                  &Size,
                  Config
                  );
  if (EFI_ERROR (Status)) {
    //
    // Absent or unexpected size: apply the driver defaults.
    //
    ZeroMem (Config, sizeof (CORE_CONFIG));
  }

  return EFI_SUCCESS;
}

/**
  Display a native setup-browser popup box.

  The popup protocol is produced by the forms browser (DisplayEngine), which
  is always loaded while a callback is running.

  @param[in]  Style      Popup style (info / warning / error).
  @param[in]  PopupType  Popup type (Ok, YesNo, ...). The box is modal and
                         waits for the user selection.
  @param[in]  Message    String token for the message (resolved against
                         mHiiHandle).

  @return  The user selection, or EfiHiiPopupSelectionCancel when the popup
           protocol is unavailable or the popup could not be shown.
**/
STATIC
EFI_HII_POPUP_SELECTION
ShowPopup (
  IN EFI_HII_POPUP_STYLE  Style,
  IN EFI_HII_POPUP_TYPE   PopupType,
  IN EFI_STRING_ID        Message
  )
{
  EFI_HII_POPUP_PROTOCOL   *Popup;
  EFI_HII_POPUP_SELECTION  Selection;
  EFI_STATUS               Status;

  Selection = EfiHiiPopupSelectionCancel;

  Status = gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid, NULL, (VOID **)&Popup);
  if (EFI_ERROR (Status) || (Popup == NULL) || (Popup->CreatePopup == NULL)) {
    return EfiHiiPopupSelectionCancel;
  }

  Status = Popup->CreatePopup (Popup, Style, PopupType, mHiiHandle, Message, &Selection);
  if (EFI_ERROR (Status)) {
    return EfiHiiPopupSelectionCancel;
  }

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
    ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeOk, STRING_TOKEN (STR_VALIDATE_NO_PROTOCOL_TEXT));
    return EFI_SUCCESS;
  }

  Status = Protocol->GetFactoryMask (Protocol, &FactoryMask);
  if (EFI_ERROR (Status)) {
    //
    // EFI_NOT_FOUND: the one-shot cold-boot learning cycle has not completed.
    //
    ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeOk, STRING_TOKEN (STR_VALIDATE_NO_FACTORY_MASK_TEXT));
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

  return EFI_SUCCESS;
}

/**
  Config access callback. Handles the "Validate Core Configuration" action
  button in the Individual Core Options submenu.

  The action is not a stored setting: it evaluates the currently selected
  per-core mask against the CCX0/CCX1 rules and reports PASS/FAIL in a native
  popup. On FAIL the user may reset the selected toggles to the factory mask.

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
  CORE_CONFIG  CoreConfig;
  UINT8        Mask;
  EFI_STATUS   Status;

  if (ActionRequest == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *ActionRequest = EFI_BROWSER_ACTION_REQUEST_NONE;

  if (Action != EFI_BROWSER_ACTION_CHANGING) {
    return EFI_UNSUPPORTED;
  }

  //
  // Restore action: confirm first, then reset the selected toggles to the
  // learned factory core mask.
  //
  if (QuestionId == CORES_RESTORE_FACTORY_QUESTION_ID) {
    if (ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeYesNo,
                   STRING_TOKEN (STR_RESTORE_FACTORY_CONFIRM_TEXT)) == EfiHiiPopupSelectionYes)
    {
      return ResetToFactoryMask ();
    }

    return EFI_SUCCESS;
  }

  if (QuestionId != CORES_VALIDATE_QUESTION_ID) {
    return EFI_UNSUPPORTED;
  }

  Status = ReadCoreConfig (&CoreConfig);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Mask = ComputeCustomMask (&CoreConfig);

  if (IsCoreMaskValid (Mask)) {
    //
    // Pass: OK popup; the browser returns to the menu on the next key press.
    //
    ShowPopup (EfiHiiPopupStyleInfo, EfiHiiPopupTypeOk, STRING_TOKEN (STR_VALIDATE_PASS_TEXT));
    return EFI_SUCCESS;
  }

  //
  // Fail: offer to reset the selected toggles to the factory core mask.
  //
  if (ShowPopup (EfiHiiPopupStyleWarning, EfiHiiPopupTypeYesNo, STRING_TOKEN (STR_VALIDATE_FAIL_TEXT)) ==
      EfiHiiPopupSelectionYes)
  {
    return ResetToFactoryMask ();
  }

  return EFI_SUCCESS;
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
  CORE_CONFIG         CoreConfig;
  EFI_GUID     HiiGuid = gEfiCallerIdGuid;

  // Workaround to avoid problems with LTO in the compiler
  volatile UINT8 LtoBypass;
  LtoBypass = BC250DXEv3MenuDriverMenuBin[0] + MeiMeiDXEv3MenuDriverStrings[0];
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

  //
  // Install the config access protocol (with our Callback) on a driver handle
  // and register the formset against it, so the setup browser can reach the
  // validate action and the varstore bridge.
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
                 MeiMeiDXEv3MenuDriverStrings,
                 BC250DXEv3MenuDriverMenuBin,
                 NULL
                 );

  if (mHiiHandle == NULL) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}