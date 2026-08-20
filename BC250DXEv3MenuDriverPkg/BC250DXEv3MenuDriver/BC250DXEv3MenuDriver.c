#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/HiiLib.h>
#include <Library/DebugLib.h>
#include "BC250DXEv3MenuDriver.h"

extern UINT8 BC250DXEv3MenuDriverMenuBin[];
extern UINT8 BC250DXEv3MenuDriverStrings[];

EFI_GUID gMenuConfigGuid = { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
EFI_HII_HANDLE mHiiHandle;

EFI_STATUS
EFIAPI
Bc250DxeV3MenuDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS   Status;
  MENU_CONFIG  Config;
  UINTN        Size = sizeof(MENU_CONFIG);
  EFI_GUID     HiiGuid = gEfiCallerIdGuid;

  // Workaround to avoid problems with LTO in the compiler
  volatile UINT8 LtoBypass;
  LtoBypass = BC250DXEv3MenuDriverMenuBin[0] + BC250DXEv3MenuDriverStrings[0];
  (VOID)LtoBypass;

  // Check to see if our variable store already exists
  Status = gRT->GetVariable(L"MeiMeiDXEv3ConfigVar", &gMenuConfigGuid, NULL, &Size, &Config);

  // If not, create it and populate it with default values
  if (Status == EFI_NOT_FOUND) {
    Config.AcpiPatch = 0;
    Config.SmuUnlock = 0;
    Config.CoreUnlock = 0;
    Config.CustomCoreMask = 0;
    Config.Core0 = 0;
    Config.Core1 = 0;
    Config.Core2 = 0;
    Config.Core3 = 0;
    Config.Core4 = 0;
    Config.Core5 = 0;
    Config.Core6 = 0;
    Config.Core7 = 0;

    Status = gRT->SetVariable(
                L"MeiMeiDXEv3ConfigVar",
                &gMenuConfigGuid,
                EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                sizeof(MENU_CONFIG),
                &Config
                );

    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  // Register our form data with HII
  mHiiHandle = HiiAddPackages (
                 &HiiGuid,
                 NULL,
                 BC250DXEv3MenuDriverStrings,
                 BC250DXEv3MenuDriverMenuBin,
                 NULL
                 );

  if (mHiiHandle == NULL) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}