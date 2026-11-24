/**
  Public protocol exposed by the BC-250 DXEv3 GPU CU Unlock driver.

  Consumers call GetBaseline() to read the learned, CRC-validated factory GPU
  configuration (the fourbanked GC registers CC/SPI/RLC/USER captured on the
  clean cold boot). The service is installed as a UEFI protocol so the menu
  driver can show factory WGP labels.

  Until the learning cycle has persisted a record
  (MeiMeiDXEv3CuFactoryBaselineVar), GetBaseline() returns EFI_NOT_FOUND.

  The protocol GUID is declared in the package .dec ([Protocols]) and is
  therefore available to any module that lists
  BC250DXEv3SMUCuUnlockPkg/BC250DXEv3SMUCuUnlockPkg.dec in its [Packages].
**/

#ifndef __MEIMEIDXEV3_CU_UNLOCK_PROTOCOL_H__
#define __MEIMEIDXEV3_CU_UNLOCK_PROTOCOL_H__

#include <Uefi.h>

//
// The baseline record layout is owned by the CU Unlock driver
// (MeiMeiDXEv3CuConfigVar.h).  The protocol keeps it opaque so consumer
// copies of this header stay self-contained and never include another
// driver's package header.
//
struct _CU_BASELINE;

typedef struct _MEIMEIDXEV3_CU_UNLOCK_PROTOCOL  MEIMEIDXEV3_CU_UNLOCK_PROTOCOL;

/**
  Read the learned factory GPU baseline.

  The record is re-read from the persisted baseline variable and validated by
  its CRC-32 fingerprint on every call. Before the one-shot cold-boot learning
  cycle has persisted a record, the service returns EFI_NOT_FOUND.

  @param[in]  This      Protocol instance pointer.
  @param[out] Baseline  Receives the factory baseline.

  @retval EFI_SUCCESS   The baseline is available.
  @retval EFI_NOT_FOUND No valid baseline yet (learning cold boot pending).
  @retval others        The baseline record could not be read.
**/
typedef
EFI_STATUS
(EFIAPI *MEIMEIDXEV3_CU_GET_BASELINE)(
  IN  MEIMEIDXEV3_CU_UNLOCK_PROTOCOL  *This,
  OUT struct _CU_BASELINE             *Baseline
  );

/**
  Read the learned factory per-bank WGP masks.

  The masks are decoded from the persisted baseline's CC registers, using the
  same 5-bit per-bank encoding the payload applies. Bit N of Masks[bank] means
  WGP N of that bank is enabled in the factory configuration. On a BC-250 cold
  boot this is typically 0x07 for each bank (3 WGPs, 24 CUs total).

  @param[in]  This   Protocol instance pointer.
  @param[out] Masks  Receives four per-bank WGP masks (5 bits used per entry).

  @retval EFI_SUCCESS   The factory masks are available.
  @retval EFI_NOT_FOUND No valid baseline yet (learning cold boot pending).
  @retval others        The baseline record could not be read.
**/
typedef
EFI_STATUS
(EFIAPI *MEIMEIDXEV3_CU_GET_FACTORY_MASK)(
  IN  MEIMEIDXEV3_CU_UNLOCK_PROTOCOL  *This,
  OUT UINT32                          *Masks
  );

struct _MEIMEIDXEV3_CU_UNLOCK_PROTOCOL {
  MEIMEIDXEV3_CU_GET_BASELINE      GetBaseline;
  MEIMEIDXEV3_CU_GET_FACTORY_MASK  GetFactoryMask;
};

#endif // __MEIMEIDXEV3_CU_UNLOCK_PROTOCOL_H__
