#ifndef __BC250DXEV3MENUDRIVER_H__
#define __BC250DXEV3MENUDRIVER_H__

//
// CoreUnlock states
//
#define CORE_UNLOCK_DISABLED   0
#define CORE_UNLOCK_ALL_CORES  1
#define CORE_UNLOCK_CUSTOM    2

//
// Question ID of the "Validate Core Configuration" action button in the
// Individual Core Options submenu (form 2). The action is not a stored
// setting; it is matched in the config access Callback.
//
#define CORES_VALIDATE_QUESTION_ID  0x5000

//
// Question ID of the "Restore Factory Core Mask" action button in the
// Individual Core Options submenu (form 2). The action is not a stored
// setting; it is matched in the config access Callback.
//
#define CORES_RESTORE_FACTORY_QUESTION_ID  0x5001

//
// CCX membership of the eight cores in the core presence mask.
//   CCX0: cores 0-3 -> bits 0-3
//   CCX1: cores 4-7 -> bits 4-7
//
#define BC250_CCX0_CORE_BITS  0x0FU
#define BC250_CCX1_CORE_BITS  0xF0U

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

#endif // __BC250DXEV3MENUDRIVER_H__