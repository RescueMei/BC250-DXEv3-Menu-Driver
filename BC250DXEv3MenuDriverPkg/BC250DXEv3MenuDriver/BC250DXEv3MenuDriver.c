#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/HiiLib.h>
#include <Library/DebugLib.h>
#include "BC250DXEv3MenuDriver.h"

extern UINT8 BC250DXEv3MenuDriverMenuBin[];
extern UINT8 MeiMeiDXEv3MenuDriverStrings[];

EFI_GUID gMenuConfigGuid = { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
EFI_HII_HANDLE mHiiHandle;

#define VARIABLE_ATTRIBUTES \
  (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE)

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

EFI_STATUS
EFIAPI
Bc250DxeV3MenuDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS   Status;
  ACPI_CONFIG  AcpiConfig;
  SMU_CONFIG   SmuConfig;
  CORE_CONFIG  CoreConfig;
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

  // Initialize the SMU configuration variable if it does not exist
  SmuConfig.SmuUnlock = 0;
  SmuConfig.SmuReportingPatch = 0;
  Status = InitVariableIfMissing (L"MeiMeiDXEv3SmuVar", &gMenuConfigGuid, &SmuConfig, sizeof (SMU_CONFIG));
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

  // Register our form data with HII
  mHiiHandle = HiiAddPackages (
                 &HiiGuid,
                 NULL,
                 MeiMeiDXEv3MenuDriverStrings,
                 BC250DXEv3MenuDriverMenuBin,
                 NULL
                 );

  if (mHiiHandle == NULL) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}