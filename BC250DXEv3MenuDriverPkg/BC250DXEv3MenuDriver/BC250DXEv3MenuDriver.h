#ifndef __BC250DXEV3MENUDRIVER_H__
#define __BC250DXEV3MENUDRIVER_H__

//
// CoreUnlock states
//
#define CORE_UNLOCK_DISABLED   0
#define CORE_UNLOCK_ALL_CORES  1
#define CORE_UNLOCK_CUSTOM    2

//
// Question IDs of the Core0..Core7 checkboxes (form 2). The VFR references
// these with questionref() to compute the per-CCX core counts shown in the
// valid/invalid status sections.
//
#define CORE0_QUESTION_ID  0x1100
#define CORE1_QUESTION_ID  0x1101
#define CORE2_QUESTION_ID  0x1102
#define CORE3_QUESTION_ID  0x1103
#define CORE4_QUESTION_ID  0x1104
#define CORE5_QUESTION_ID  0x1105
#define CORE6_QUESTION_ID  0x1106
#define CORE7_QUESTION_ID  0x1107

//
// Question ID of the "Restore Factory Core Mask" action button in the
// Individual Core Options submenu (form 2). The action is not a stored
// setting; it is matched in the config access Callback.
//
#define CORES_RESTORE_FACTORY_QUESTION_ID  0x5001

//
// ACPI configuration variable
//
typedef struct {
  UINT8  AcpiPatch;
} ACPI_CONFIG;

//
// SMU configuration variables (one per SMU driver)
//
typedef struct {
  UINT8  SmuUnlock;
} SMU_UNLOCK_CONFIG;

typedef struct {
  UINT8  SmuReportingPatch;
} SMU_PATCH_CONFIG;

//
// Core configuration variable
//
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

//
// SMU governor variables. The enable is a separate fail-closed flag (the
// governor driver reads its first byte); the config holds the operating
// frequency window and the V(f) safe-point curve that the governor maps onto
// the SMU AVFS tables at boot. Each of the 15 points may be enabled
// independently; disabled points are ignored.
//
#define SMU_GOVERNOR_ENABLE_VAR_NAME  L"MeiMeiDXEv3SmuGovernorEnable"
#define SMU_GOVERNOR_CONFIG_VAR_NAME  L"MeiMeiDXEv3SmuGovernorConfig"

#define SMU_SAFE_POINT_COUNT    15
#define SMU_SAFE_POINTS_SCHEMA  1

typedef struct {
  UINT8  SmuGovernor;   // 0 = disabled, non-zero = enabled
} SMU_GOVERNOR_ENABLE_CONFIG;

typedef struct {
  UINT8   Enable;      // 0 = ignore this point, non-zero = use it
  UINT8   Reserved;    // explicit padding so the layout is compiler-independent
  UINT16  Frequency;   // MHz
  UINT16  Voltage;     // mV
} SMU_SAFE_POINT;

typedef struct {
  UINT16          Schema;          // SMU_SAFE_POINTS_SCHEMA
  UINT16          Count;           // number of valid Point entries
  UINT16          MinFrequency;    // MHz
  UINT16          MaxFrequency;    // MHz
  SMU_SAFE_POINT  Point[SMU_SAFE_POINT_COUNT];
} SMU_GOVERNOR_CONFIG;

#endif // __BC250DXEV3MENUDRIVER_H__