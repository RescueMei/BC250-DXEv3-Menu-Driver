# BC250-DXEv3-Menu-Driver

Driver for my v3 bios to add options that will not interfere with existing options in the BIOS. It registers an HII formset that exposes a set of options in the BIOS setup menu for the MeiMeiDXEv3 family of drivers (ACPI patching, SMU unlock, SMU reporting patch, core unlock and per-core selection, and the in-SMU GPU governor).

The driver's purpose is only to present and store the configuration below. It does not apply the options itself; other drivers should read the variables and act on them.

## Menu Layout

Every option lives in the single formset **"MeiMeiDXEv3 Menu"** (class `4`). Options are grouped into four sections:

| Section | Option | Variable | Gated by |
| --- | --- | --- | --- |
| Patch Options | ACPI Patch | `MeiMeiDXEv3AcpiVar` | — |
| SMU Options | SMU Unlock | `MeiMeiDXEv3SmuUnlockVar` | — |
| SMU Options | SMU Reporting Patch | `MeiMeiDXEv3SmuPatchVar` | SMU Unlock = 1 |
| GPU Options | In-SMU Governor | `MeiMeiDXEv3SmuGovernorVar` | SMU Unlock = 1 |
| GPU Options | In-SMU Governor Options (submenu) | `MeiMeiDXEv3SmuGovernorVar` | In-SMU Governor = 1 |
| CPU Options | Core Unlock | `MeiMeiDXEv3CoreVar` | SMU Unlock = 1 |
| CPU Options | Individual Core Options (submenu) | `MeiMeiDXEv3CoreVar` | Core Unlock = Custom |

The two submenus are separate forms:

- **Individual Core Options** — the CCX0/CCX1 per-core toggles, a live valid/invalid status block, and the *Restore Factory Core Mask* action.
- **In-SMU Governor Options** — package and GPU power limits, plus 15 *Freq Volt* entries, each with an enable toggle plus a frequency (MHz) and voltage (mV). The governor derives its clock window (Min/Max) from the enabled points; there are no separate Min/Max fields.

All option questions are marked `RESET_REQUIRED`, so the setup browser offers/requires a system reset before exiting setup whenever any option is changed.

## Configuration Variables

The menu front-end (VFR) stores settings in five separate non-volatile UEFI variables. All five share the same vendor GUID; they differ only in variable name. Each variable holds a single small struct, so the layout of one feature can evolve independently of the others.

| Variable name | Struct | Size | Contents |
| --- | --- | --- | --- |
| `MeiMeiDXEv3AcpiVar` | `ACPI_CONFIG` | 1 byte | `AcpiPatch` |
| `MeiMeiDXEv3SmuUnlockVar` | `SMU_UNLOCK_CONFIG` | 1 byte | `SmuUnlock` |
| `MeiMeiDXEv3SmuPatchVar` | `SMU_PATCH_CONFIG` | 1 byte | `SmuReportingPatch` |
| `MeiMeiDXEv3CoreVar` | `CORE_CONFIG` | 9 bytes | `CoreUnlock`, `Core0`–`Core7` |
| `MeiMeiDXEv3SmuGovernorVar` | `SMU_GOVERNOR_CONFIG` | 96 bytes | `SMUGovernorEnable`, `GovernorFreqVoltPoints[15]`, `PackagePowerLimitW`, `GpuPowerLimitW` |

Shared properties:

| Item | Value |
| --- | --- |
| Vendor GUID | `49CC168D-E8B0-4613-A807-16969986726F` |
| Attributes | `EFI_VARIABLE_BOOTSERVICE_ACCESS` \| `EFI_VARIABLE_RUNTIME_ACCESS` \| `EFI_VARIABLE_NON_VOLATILE` |

If a variable does not already exist when the driver starts, it is created and initialized with its defaults (see **Defaults** below).

## Variable Contents

Multi-byte fields are little-endian and naturally aligned; the offsets below are byte offsets from the start of that variable's data. `UINT8` fields are a single byte.

### ACPI_CONFIG (`MeiMeiDXEv3AcpiVar`, 1 byte)

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `AcpiPatch` | Enable the ACPI patch (`0` = disabled, `1` = enabled). |

### SMU_UNLOCK_CONFIG (`MeiMeiDXEv3SmuUnlockVar`, 1 byte)

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `SmuUnlock` | SMU unlock. Required for the SMU reporting patch, the GPU options, and the core options below (`0` = disabled, `1` = enabled). |

### SMU_PATCH_CONFIG (`MeiMeiDXEv3SmuPatchVar`, 1 byte)

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `SmuReportingPatch` | Patches the SMU for 8-core reporting (`0` = disabled, `1` = enabled). |

### CORE_CONFIG (`MeiMeiDXEv3CoreVar`, 9 bytes)

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `CoreUnlock` | Core unlock mode. See **CoreUnlock states** below. |
| 1 | `Core0` | Enable Core 0 (`0` = disabled, `1` = enabled). |
| 2 | `Core1` | Enable Core 1 (`0` = disabled, `1` = enabled). |
| 3 | `Core2` | Enable Core 2 (`0` = disabled, `1` = enabled). |
| 4 | `Core3` | Enable Core 3 (`0` = disabled, `1` = enabled). |
| 5 | `Core4` | Enable Core 4 (`0` = disabled, `1` = enabled). |
| 6 | `Core5` | Enable Core 5 (`0` = disabled, `1` = enabled). |
| 7 | `Core6` | Enable Core 6 (`0` = disabled, `1` = enabled). |
| 8 | `Core7` | Enable Core 7 (`0` = disabled, `1` = enabled). |

### SMU_GOVERNOR_CONFIG (`MeiMeiDXEv3SmuGovernorVar`, 96 bytes)

| Offset | Field | Description |
| --- | --- | --- |
| 0 | `SMUGovernorEnable` | Enable the in-SMU GPU governor payload (`0` = disabled, `1` = enabled). Fail-closed: the BC250-DXEv3 SMU Governor driver reads this byte and only injects when it is non-zero. |
| 1 | `Reserved` | Explicit padding so the layout is compiler-independent. |
| 2 | `GovernorFreqVoltPoints[15]` | 15 × `SMU_SAFE_POINT` (6 bytes each); see below. |
| 92 | `PackagePowerLimitW` | Raw package/socket power limit in watts. `0` uses the governor default (250 W). |
| 94 | `GpuPowerLimitW` | Raw VDDCR_GFX voltage × current power limit in watts. `0` uses the governor default (200 W). |

`SMU_SAFE_POINT` (6 bytes):

| Offset (within entry) | Field | Description |
| --- | --- | --- |
| 0 | `Enable` | Use this point (`0` = ignored, non-zero = used). |
| 1 | `Reserved` | Explicit padding so the layout is compiler-independent. |
| 2 | `Frequency` | Frequency in MHz. |
| 4 | `Voltage` | Voltage in mV. |

`GovernorFreqVoltPoints[0]` starts at byte 2; `GovernorFreqVoltPoints[N]` is at `2 + N * 6`. The curve is expected to be stored in ascending frequency order. The governor's clock window (Min/Max) is derived from the enabled points, not stored separately.

### CoreUnlock states

`CoreUnlock` is a three-state selection in the setup menu:

| Value | Constant | Meaning |
| --- | --- | --- |
| 0 | `CORE_UNLOCK_DISABLED` | Disabled (factory settings; core unlock not applied). |
| 1 | `CORE_UNLOCK_ALL_CORES` | Unlock all cores. |
| 2 | `CORE_UNLOCK_CUSTOM` | Core unlock is enabled with the per-core selection (`Core0` – `Core7`). The "Individual Core Options" submenu is only selectable in this mode. |

## Defaults

Defaults are written only when a variable does not exist yet (first boot / after clearing NVRAM). They are not re-applied afterward.

| Variable | Default |
| --- | --- |
| `MeiMeiDXEv3AcpiVar` | `AcpiPatch = 0` |
| `MeiMeiDXEv3SmuUnlockVar` | `SmuUnlock = 0` |
| `MeiMeiDXEv3SmuPatchVar` | `SmuReportingPatch = 0` |
| `MeiMeiDXEv3CoreVar` | `CoreUnlock = CORE_UNLOCK_DISABLED`, all cores `0` |
| `MeiMeiDXEv3SmuGovernorVar` | `SMUGovernorEnable = 0` (disabled), `PackagePowerLimitW = 250`, `GpuPowerLimitW = 200`, and the 15 points in the table below (the governor derives its Min/Max from the enabled points). |

Default safe points (`Frequency` MHz / `Voltage` mV / `Enable`):

| # | Freq | Volt | Enable |
| --- | --- | --- | --- |
| 1 | 350 | 700 | no |
| 2 | 500 | 700 | yes |
| 3 | 1175 | 700 | yes |
| 4 | 1400 | 750 | yes |
| 5 | 1600 | 800 | yes |
| 6 | 1700 | 850 | yes |
| 7 | 1800 | 875 | yes |
| 8 | 1850 | 900 | no |
| 9 | 2000 | 950 | no |
| 10 | 2050 | 975 | no |
| 11 | 2100 | 1000 | no |
| 12 | 2125 | 1015 | no |
| 13 | 2150 | 1030 | no |
| 14 | 2200 | 1050 | no |
| 15 | 0 | 0 | no |

The canonical definitions live in `BC250DXEv3MenuDriverPkg/BC250DXEv3MenuDriver/BC250DXEv3MenuDriver.h`:

```c
#define CORE_UNLOCK_DISABLED   0
#define CORE_UNLOCK_ALL_CORES  1
#define CORE_UNLOCK_CUSTOM     2

typedef struct {
  UINT8  AcpiPatch;
} ACPI_CONFIG;

typedef struct {
  UINT8  SmuUnlock;
} SMU_UNLOCK_CONFIG;

typedef struct {
  UINT8  SmuReportingPatch;
} SMU_PATCH_CONFIG;

typedef struct {
  UINT8  CoreUnlock;
  UINT8  Core0;
  UINT8  Core1;
  UINT8  Core2;
  UINT8  Core3;
  UINT8  Core4;
  UINT8  Core5;
  UINT8  Core6;
  UINT8  Core7;
} CORE_CONFIG;

#define SMU_SAFE_POINT_COUNT    15

typedef struct {
  UINT8   Enable;      // 0 = ignore this point, non-zero = use it
  UINT8   Reserved;    // explicit padding so the layout is compiler-independent
  UINT16  Frequency;   // MHz
  UINT16  Voltage;     // mV
} SMU_SAFE_POINT;

typedef struct {
  UINT8           SMUGovernorEnable;   // offset 0
  UINT8           Reserved;            // padding
  SMU_SAFE_POINT  GovernorFreqVoltPoints[SMU_SAFE_POINT_COUNT];
  UINT16          PackagePowerLimitW;  // offset 92, 0 = default
  UINT16          GpuPowerLimitW;      // offset 94, 0 = default
} SMU_GOVERNOR_CONFIG;
```

## Using These Variables From Other Drivers

Consuming the variables is done with the standard runtime services `GetVariable()` / `SetVariable()` calls. Since the variables have `RUNTIME_ACCESS`, they can be used by any `DXE_DRIVER` (or later stage) and even after `ExitBootServices`.

### Reading the configuration

Include the header (or copy the structs/GUID into your driver) and read the variable you need in one call:

```c
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "BC250DXEv3MenuDriver.h"   // defines ACPI_CONFIG, SMU_UNLOCK_CONFIG, SMU_PATCH_CONFIG, CORE_CONFIG, SMU_SAFE_POINT, SMU_GOVERNOR_CONFIG
#include "Guid/MeiMeiDXEv3ConfigVar.h"  // defines gMeiMeiDXEv3ConfigVarGuid (see below)

EFI_STATUS
ReadCoreConfig (
  OUT CORE_CONFIG  *Config
  )
{
  UINTN       Size = sizeof (CORE_CONFIG);
  EFI_STATUS  Status;

  Status = gRT->GetVariable (
                  L"MeiMeiDXEv3CoreVar",
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
    // driver's defaults apply.
    ZeroMem (Config, sizeof (CORE_CONFIG));
    return EFI_SUCCESS;
  }

  return EFI_SUCCESS;
}
```

The in-SMU governor gate is the `SMUGovernorEnable` byte of `MeiMeiDXEv3SmuGovernorVar` (fail-closed):

```c
  SMU_GOVERNOR_CONFIG  GovConfig;
  UINTN                Size = sizeof (GovConfig);

  if (!EFI_ERROR (gRT->GetVariable (L"MeiMeiDXEv3SmuGovernorVar",
                                    &gMeiMeiDXEv3ConfigVarGuid, NULL,
                                    &Size, &GovConfig)) &&
      (GovConfig.SMUGovernorEnable != 0)) {
    // governor enabled; GovConfig.GovernorFreqVoltPoints[] holds the curve
  }
```

Usage:

```c
  CORE_CONFIG         CoreConfig;
  ACPI_CONFIG         AcpiConfig;
  SMU_UNLOCK_CONFIG   SmuUnlockConfig;
  SMU_PATCH_CONFIG    SmuPatchConfig;

  if (!EFI_ERROR (ReadCoreConfig (&CoreConfig))) {
    if (CoreConfig.CoreUnlock == CORE_UNLOCK_CUSTOM) {
      // Respect the per-core toggles Core0..Core7
    } else if (CoreConfig.CoreUnlock == CORE_UNLOCK_ALL_CORES) {
      // Core unlock enabled with the driver's default behavior
    }
  }
```

### Writing the configuration

Set the same attributes the menu driver uses, so the HII form can still see and edit the values:

```c
  SMU_UNLOCK_CONFIG  SmuUnlockConfig;
  SMU_PATCH_CONFIG   SmuPatchConfig;
  CORE_CONFIG        CoreConfig;

  SmuUnlockConfig.SmuUnlock = 1;
  Status = gRT->SetVariable (
                  L"MeiMeiDXEv3SmuUnlockVar",
                  &gMeiMeiDXEv3ConfigVarGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS     |
                  EFI_VARIABLE_NON_VOLATILE,
                  sizeof (SMU_UNLOCK_CONFIG),
                  &SmuUnlockConfig
                  );

  SmuPatchConfig.SmuReportingPatch = 1;
  Status = gRT->SetVariable (
                  L"MeiMeiDXEv3SmuPatchVar",
                  &gMeiMeiDXEv3ConfigVarGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS     |
                  EFI_VARIABLE_NON_VOLATILE,
                  sizeof (SMU_PATCH_CONFIG),
                  &SmuPatchConfig
                  );

  CoreConfig.CoreUnlock = CORE_UNLOCK_CUSTOM;
  CoreConfig.Core0      = 1;
  Status = gRT->SetVariable (
                  L"MeiMeiDXEv3CoreVar",
                  &gMeiMeiDXEv3ConfigVarGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS     |
                  EFI_VARIABLE_NON_VOLATILE,
                  sizeof (CORE_CONFIG),
                  &CoreConfig
                  );
```

### GUID definition

If you do not link against this package's header, define the GUID yourself (must match exactly):

```c
EFI_GUID  gMeiMeiDXEv3ConfigVarGuid =
  { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };
```

Alternatively, if you cannot use the structs, access the fields by raw byte offsets as shown in the tables above.

## Notes

- Each variable is non-volatile and persists across reboots; it is only created (and defaulted) if it does not exist when the menu driver first runs.
- All five variables share one vendor GUID but differ by name, letting each feature's layout evolve without affecting the others.
- The menu driver only stores the configuration — apply/decode the options in your own driver where appropriate.
- All SMU governor options (enable, frequency window, V(f) points, and package/GPU power limits) live in the single `MeiMeiDXEv3SmuGovernorVar`; the BC250-DXEv3 SMU Governor driver gates injection on its `SMUGovernorEnable` byte and can read the curve and limits from the same record.
- The Individual Core Options submenu shows a live status block above the restore button. When the selected cores satisfy the CCX0/CCX1 rules (CCX0 must keep at least one core enabled, and CCX1 must have the same number of enabled cores as CCX0 or be empty), a "Valid Core Configuration" section is shown; otherwise an "Invalid Core Configuration" warning with the CCX requirements is shown. These are declarative `suppressif`-gated `subtitle`/`text` statements driven by `questionref()` sums over the Core0-Core7 checkboxes, so they update automatically as cores are toggled (no callback involved).
- The submenu also has a "Restore Factory Core Mask" action button. It asks for confirmation and, on Yes, resets the currently selected Core0-Core7 toggles to the learned factory core mask (read through `gMeiMeiDXEv3CoreUnlockProtocolGuid` / `GetFactoryMask`, produced by the BC-250 DXEv3 Core Unlock driver). If that driver is absent, or the factory mask has not been learned yet, an informational popup is shown and nothing is reset.
- Watch out for `EFI_BUFFER_TOO_SMALL` if a variable grows in the future; always pass `sizeof (STRUCT)` for `DataSize`.
