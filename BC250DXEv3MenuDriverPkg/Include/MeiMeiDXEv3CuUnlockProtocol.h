/** @file
  Public protocol exposed by the BC-250 DXEv3 GPU CU Unlock driver.

  The menu driver uses GetFactoryMask() to restore the per-WGP selection to the
  learned, CRC-validated factory CU configuration. The protocol layout mirrors
  the CU Unlock driver's own interface; GetBaseline() is present only to keep
  the function table layout identical.

  NOTE: This interface is vendored here so this package stays self-contained.
  The protocol GUID is declared in this package's .dec ([Protocols]) and MUST
  stay in sync with the CU Unlock driver's own .dec declaration:
    gMeiMeiDXEv3CuUnlockProtocolGuid =
      { 0x3b6f9c24, 0x7d51, 0x4a8e,
        { 0xb2, 0xc3, 0x1f, 0x0d, 0x6e, 0x8a, 0x44, 0x17 } }
**/

#ifndef __MEIMEIDXEV3_CU_UNLOCK_PROTOCOL_H__
#define __MEIMEIDXEV3_CU_UNLOCK_PROTOCOL_H__

#include <Uefi.h>

typedef struct _MEIMEIDXEV3_CU_UNLOCK_PROTOCOL  MEIMEIDXEV3_CU_UNLOCK_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *MEIMEIDXEV3_CU_GET_BASELINE)(
  IN  MEIMEIDXEV3_CU_UNLOCK_PROTOCOL  *This,
  OUT VOID                            *Baseline
  );

/**
  Read the learned factory per-bank WGP masks (5 bits used per entry).

  @param[in]  This   Protocol instance pointer.
  @param[out] Masks  Receives four per-bank WGP masks.

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
