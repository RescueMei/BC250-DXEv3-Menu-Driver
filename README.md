# BC250-DXEv3-Menu-Driver

Driver for my v3 bios to add options that will not interfere with existing options in the BIOS. It registers an HII formset that exposes a set of options in the BIOS setup menu for the MeiMeiDXEv3 family of drivers (ACPI patching, SMU unlock, core unlock, and per-core selection).

The driver's purpose is only to present and store the configuration below. It does not apply the options itself; other drivers should read this variable and act on it.

## Configuration Variable

The menu front-end (VFR) stores all of its settings in a single UEFI variable:

| Item | Value |
| --- | --- |
| Variable name | `MeiMeiDXEv3ConfigVar` |
| Vendor GUID | `49CC168D-E8B0-4613-A807-16969986726F` |
| Attributes | `EFI_VARIABLE_BOOTSERVICE_ACCESS` \| `EFI_VARIABLE_RUNTIME_ACCESS` \| `EFI_VARIABLE_NON_VOLATILE` |
| Size | 12 bytes (see table below) |

If the variable does not already exist when the driver starts, it is created and initialized with all fields set to `0`.

## Variable Contents (MENU_CONFIG)

The variable holds a `MENU_CONFIG` structure. Every field is a single-byte `UINT8` used as a boolean (`0` = disabled, `1` = enabled). Offsets are byte offsets from the start of the variable data:

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `AcpiPatch` | Enable the ACPI patch. |
| 1 | `SmuUnlock` | SMU unlock. Required for all options below. |
| 2 | `CoreUnlock` | Unlock additional cores. |
| 3 | `CustomCoreMask` | Use the custom core mask settings below. |
| 4 | `Core0` | Enable Core 0. |
| 5 | `Core1` | Enable Core 1. |
| 6 | `Core2` | Enable Core 2. |
| 7 | `Core3` | Enable Core 3. |
| 8 | `Core4` | Enable Core 4. |
| 9 | `Core5` | Enable Core 5. |
| 10 | `Core6` | Enable Core 6. |
| 11 | `Core7` | Enable Core 7. |

The canonical definition lives in `BC250DXEv3MenuDriverPkg/BC250DXEv3MenuDriver/BC250DXEv3MenuDriver.h`:

```c
typedef struct {
  UINT8  AcpiPatch;
  UINT8  SmuUnlock;
  UINT8  CoreUnlock;
  UINT8  CustomCoreMask;
  UINT8  Core0;
  UINT8  Core1;
  UINT8  Core2;
  UINT8  Core3;
  UINT8  Core4;
  UINT8  Core5;
  UINT8  Core6;
  UINT8  Core7;
} MENU_CONFIG;
```

## Using This Variable From Other Drivers

Consuming this variable is done with the standard runtime services `GetVariable()` / `SetVariable()` calls. Since the variable has `RUNTIME_ACCESS`, it can be used by any `DXE_DRIVER` (or later stage) and even after `ExitBootServices`.

### Reading the configuration

Include the header (or copy the struct/GUID into your driver) and read the whole structure in one call:

```c
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "BC250DXEv3MenuDriver.h"   // defines MENU_CONFIG
#include "Guid/MeiMeiDXEv3ConfigVar.h"  // defines gMeiMeiDXEv3ConfigVarGuid (see below)

EFI_STATUS
ReadMenuConfig (
  OUT MENU_CONFIG  *Config
  )
{
  UINTN       Size = sizeof (MENU_CONFIG);
  EFI_STATUS  Status;

  Status = gRT->GetVariable (
                  L"MeiMeiDXEv3ConfigVar",
                  &gMeiMeiDXEv3ConfigVarGuid,
                  NULL,
                  &Size,
                  Config
                  );

  if (Status == EFI_BUFFER_TOO_SMALL) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (EFI_ERROR (Status)) {
    // EFI_NOT_FOUND or other error: the variable is absent, so the
    // driver's defaults (all zeros) apply.
    ZeroMem (Config, sizeof (MENU_CONFIG));
    return EFI_SUCCESS;
  }

  return EFI_SUCCESS;
}
```

Usage:

```c
  MENU_CONFIG  Config;
  if (!EFI_ERROR (ReadMenuConfig (&Config))) {
    if (Config.SmuUnlock != 0) {
      // SMU unlock is enabled in the menu
    }
    if (Config.CustomCoreMask != 0) {
      // Respect the per-core toggles Core0..Core7
    }
  }
```

### Writing the configuration

Set the same attributes the menu driver uses, so the HII form can still see and edit the values:

```c
  Config.SmuUnlock = 1;
  Config.Core0     = 1;

  Status = gRT->SetVariable (
                  L"MeiMeiDXEv3ConfigVar",
                  &gMeiMeiDXEv3ConfigVarGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS     |
                  EFI_VARIABLE_NON_VOLATILE,
                  sizeof (MENU_CONFIG),
                  &Config
                  );
```

### GUID definition

If you do not link against this package's header, define the GUID yourself (must match exactly):

```c
EFI_GUID  gMeiMeiDXEv3ConfigVarGuid =
  { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
```

Alternatively, if you cannot use the struct, access the fields by raw byte offsets as shown in the table above.

## Notes

- The variable is non-volatile and persists across reboots; it is only created (and defaulted to all zeros) if it does not exist when the menu driver first runs.
- Fields are shared across the MeiMeiDXEv3 family of drivers. All consumers must agree on the same variable name, GUID, and layout.
- The menu driver only stores the configuration — apply/decode the options in your own driver where appropriate.
- Watch out for `EFI_BUFFER_TOO_SMALL` if the variable grows in the future; always pass `sizeof (MENU_CONFIG)` for `DataSize`.