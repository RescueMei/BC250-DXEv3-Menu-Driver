#ifndef __BC250DXEV3MENUDRIVER_H__
#define __BC250DXEV3MENUDRIVER_H__

//
// CoreUnlock states
//
#define CORE_UNLOCK_DISABLED   0
#define CORE_UNLOCK_ALL_CORES  1
#define CORE_UNLOCK_CUSTOM    2

//
// ACPI configuration variable
//
typedef struct {
  UINT8  AcpiPatch;
} ACPI_CONFIG;

//
// SMU configuration variable
//
typedef struct {
  UINT8  SmuUnlock;
  UINT8  SmuReportingPatch;
} SMU_CONFIG;

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