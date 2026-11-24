/* VFR-safe config mirror. Keep layouts in sync with MeiMeiDXEv3Config.h. */
#ifndef BC250DXEV3_MENU_VFR_H_
#define BC250DXEV3_MENU_VFR_H_

#include <Ui/MeiMeiDXEv3UiIds.h>

#define CORE_UNLOCK_DISABLED   0
#define CORE_UNLOCK_ALL_CORES  1
#define CORE_UNLOCK_CUSTOM     2

#define CU_UNLOCK_FACTORY  0
#define CU_UNLOCK_ALL      1
#define CU_UNLOCK_CUSTOM   2
#define CU_CONFIG_SCHEMA   1

#define SMU_SAFE_POINT_COUNT 15

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

typedef struct {
  UINT8   Enable;
  UINT8   Reserved;
  UINT16  Frequency;
  UINT16  Voltage;
} SMU_SAFE_POINT;

typedef struct {
  UINT8           SMUGovernorEnable;
  UINT8           Reserved;
  SMU_SAFE_POINT  GovernorFreqVoltPoints[SMU_SAFE_POINT_COUNT];
  UINT16          PackagePowerLimitW;
  UINT16          GpuPowerLimitW;
} SMU_GOVERNOR_CONFIG;

typedef struct {
  UINT8  Version;
  UINT8  Mode;
  UINT8  Reserved[2];
  UINT8  Wgp[20];
} CU_CONFIG;

#endif
