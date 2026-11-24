# BC250-DXEv3-Menu-Driver

The single UI owner for the DXEv3 family. It registers an HII formset under
**Advanced -> MeiMeiDXEv3 Menu**, stores configuration in six non-volatile UEFI
variables, and never applies feature settings itself. Other drivers read the
variables and act on them.

## At a glance

| Item | Value |
| --- | --- |
| Package / module | `BC250DXEv3MenuDriverPkg` -> `MeiMeiDXEv3_Menu_Driver.efi` |
| Depex | `gEfiHiiDatabaseProtocolGuid AND gEfiHiiConfigRoutingProtocolGuid` |
| Produces | `EFI_HII_CONFIG_ACCESS_PROTOCOL` plus the formset |
| Consumes | Core/CU factory-mask protocols for the restore actions |
| Configuration | six variables, shared vendor GUID |
| SMU SRAM | none |
| Compliance | [COMPLIANCE.md](COMPLIANCE.md) |

## Dependencies

HII database and config-routing services are in the Depex. The restore actions
look up `gMeiMeiDXEv3CoreUnlockProtocolGuid` and
`gMeiMeiDXEv3CuUnlockProtocolGuid` at runtime; if a driver or a learned factory
record is absent, the action shows an informational popup and changes nothing.

## How it works

The formset is organized as follows.

All options live in the single formset **"MeiMeiDXEv3 Menu"**.

| Section | Option | Variable | Gated by |
| --- | --- | --- | --- |
| Patch Options | ACPI Patch | `MeiMeiDXEv3AcpiVar` | - |
| SMU Options | SMU Unlock | `MeiMeiDXEv3SmuUnlockVar` | - |
| SMU Options | SMU Reporting Patch | `MeiMeiDXEv3SmuPatchVar` | SMU Unlock = 1 |
| GPU Options | In-SMU Governor | `MeiMeiDXEv3SmuGovernorVar` | SMU Unlock = 1 |
| GPU Options | In-SMU Governor Options | `MeiMeiDXEv3SmuGovernorVar` | Governor = 1 |
| GPU Options | Compute Unlock | `MeiMeiDXEv3CuVar` | SMU Unlock = 1 |
| GPU Options | Individual WGPs | `MeiMeiDXEv3CuVar` | CU mode = Custom |
| CPU Options | Core Unlock | `MeiMeiDXEv3CoreVar` | SMU Unlock = 1 |
| CPU Options | Individual Core Options | `MeiMeiDXEv3CoreVar` | Core mode = Custom |

Submenus:

- **In-SMU Governor Options** - package and GPU power limits plus 15 Freq/Volt
  entries. The governor derives its Min/Max window from the enabled points.
- **Individual WGPs** - 20 WGP toggles (four banks of five), a live valid/invalid
  status block, and *Restore Factory CU Mask*.
- **Individual Core Options** - per-core toggles, a live valid/invalid status
  block, and *Restore Factory Core Mask*.

All options are marked `RESET_REQUIRED`, so the browser asks for a reset when a
setting changes.

### Core rules

Cores 0-3 are CCX0, cores 4-7 are CCX1. CCX0 must keep at least one core
enabled. CCX1 must enable either zero cores or the same number as CCX0.

### CU rules

CUs are enabled per WGP (two CUs per WGP). A custom profile must keep at least
one WGP enabled in every bank. Custom mode uses `Wgp[20]`, indexed
`bank * 5 + wgp`.

## Configuration

All six share the vendor GUID and attributes from the shared config ABI:

| Variable | Structure | Size |
| --- | --- | --- |
| `MeiMeiDXEv3AcpiVar` | `ACPI_CONFIG` | 1 B |
| `MeiMeiDXEv3SmuUnlockVar` | `SMU_UNLOCK_CONFIG` | 1 B |
| `MeiMeiDXEv3SmuPatchVar` | `SMU_PATCH_CONFIG` | 1 B |
| `MeiMeiDXEv3CoreVar` | `CORE_CONFIG` | 9 B |
| `MeiMeiDXEv3SmuGovernorVar` | `SMU_GOVERNOR_CONFIG` | 96 B |
| `MeiMeiDXEv3CuVar` | `CU_CONFIG` | 24 B |

Canonical definitions, names, GUID, and attributes:
`BC250-DXEv3-Shared/MeiMeiDXEv3SharedPkg/Include/Config/MeiMeiDXEv3Config.h`.
Do not copy the structs into another driver; include the shared header.

Layout notes that matter to the UI:

- `SMU_GOVERNOR_CONFIG`: byte 0 `SMUGovernorEnable`, byte 1 reserved, bytes 2-91
  fifteen 6-byte `SMU_SAFE_POINT` entries, bytes 92-93 `PackagePowerLimitW`,
  bytes 94-95 `GpuPowerLimitW`. Zero power limits select the 250 W / 200 W
  defaults.
- `SMU_SAFE_POINT`: byte 0 enable, byte 1 reserved, bytes 2-3 frequency (MHz),
  bytes 4-5 voltage (mV).
- `CU_CONFIG`: byte 0 version (1), byte 1 mode (0 factory, 1 all, 2 custom),
  bytes 2-3 reserved, bytes 4-23 `Wgp[20]`.
- `CORE_CONFIG`: byte 0 mode (0 disabled, 1 all, 2 custom), bytes 1-8 `Core0`..`Core7`.

`BC250DXEv3MenuVfr.h` is a VFR-safe mirror of the structs the formset needs. It
is compared against the shared header at build time by
`BC250-DXEv3-Shared/tools/check_vfr_mirror.py`; do not edit it in isolation.

## Defaults

Variables are created only when absent; defaults are not re-applied afterwards.

| Variable | Default |
| --- | --- |
| `MeiMeiDXEv3AcpiVar` | disabled |
| `MeiMeiDXEv3SmuUnlockVar` | disabled |
| `MeiMeiDXEv3SmuPatchVar` | disabled |
| `MeiMeiDXEv3CoreVar` | disabled, all cores off |
| `MeiMeiDXEv3CuVar` | factory |
| `MeiMeiDXEv3SmuGovernorVar` | disabled, 250 W / 200 W, curve below |

Default governor curve (`Frequency` MHz / `Voltage` mV / enabled):

| # | Freq | Volt | On | # | Freq | Volt | On |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 350 | 700 | no | 9 | 2000 | 950 | no |
| 2 | 500 | 700 | yes | 10 | 2050 | 975 | no |
| 3 | 1175 | 700 | yes | 11 | 2100 | 1000 | no |
| 4 | 1400 | 750 | yes | 12 | 2125 | 1015 | no |
| 5 | 1600 | 800 | yes | 13 | 2150 | 1030 | no |
| 6 | 1700 | 850 | yes | 14 | 2200 | 1050 | no |
| 7 | 1800 | 875 | yes | 15 | - | - | no |
| 8 | 1850 | 900 | no | | | | |

The canonical values are `mMeiMeiDefaultGovernorConfig` in
`MeiMeiConfigLib`, which the menu and the governor both use.

## Reading the configuration from another driver

Use the shared names, record types, and attribute constant. Validate size and
attributes rather than accepting any value:

```c
#include <Config/MeiMeiDXEv3Config.h>

CORE_CONFIG  Config;
UINTN        Size = sizeof (Config);
UINT32       Attributes;

Status = gRT->GetVariable (MEIMEIDXEV3_CORE_VAR_NAME,
                           &gMeiMeiDXEv3ConfigVarGuid,
                           &Attributes, &Size, &Config);
if (Status == EFI_NOT_FOUND) {
  ZeroMem (&Config, sizeof (Config));       /* disabled */
} else if (EFI_ERROR (Status) ||
           Size != sizeof (Config) ||
           Attributes != MEIMEIDXEV3_CONFIG_VAR_ATTRIBUTES) {
  ZeroMem (&Config, sizeof (Config));       /* fail closed */
}
```

## Protocols

Produces `EFI_HII_CONFIG_ACCESS_PROTOCOL` on the driver handle plus the
`MeiMeiDXEv3 Menu` formset. It consumes the Core and CU factory-mask protocols
for the restore actions; both are optional and looked up at runtime.

## SMU SRAM placement

None. The menu only stores configuration. See
[SMU_MEMORY_MAP.md](https://github.com/RescueMei/BC250-DXEv3-Shared/blob/main/docs/SMU_MEMORY_MAP.md).

## Build

The formset is compiled with the VFR compiler as part of the normal module
build. The menu package also builds the default-creation and validation library
from the shared package.

```bash
./build_ffs.sh                                  # container build
NO_CONTAINER=1 EDK2_DIR=/path/to/edk2 ./build_ffs.sh   # host build
```

Output:

- `Build/Output/MeiMeiDXEv3_Menu_Driver.efi`
- `Build/Output/MeiMeiDXEv3_Menu_Driver.ffs`

## Validation

2026-11-24, eight-driver stack: the menu driver was present in the validated
build, the shared ABI and VFR-mirror checks passed at build time, and the
integrated 2100 MHz run completed with `DEPLOY_OK` and a reachable board. See
[COMPLIANCE.md](COMPLIANCE.md).
