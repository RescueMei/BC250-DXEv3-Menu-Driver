/** @file
  Public protocol exposed by the BC-250 DXEv3 SMU Core Unlock driver.

  Consumers call GetFactoryMask() to read the learned, CRC-validated factory
  core presence mask (SMN 0x5A870 low byte). The service is installed as a
  UEFI protocol so other DXE drivers can depend on it through the protocol
  GUID and query the factory mask at runtime.

  The factory mask is the value captured on the clean cold boot after the
  one-shot learning cycle; it is also the mask restored when CoreUnlock is
  DISABLED. Until the learning cycle has persisted a record
  (MeiMeiDXEv3CoreFactoryMaskVar), GetFactoryMask() returns EFI_NOT_FOUND.

  NOTE: This interface is vendored here so this package stays self-contained.
  The protocol GUID is declared in this package's .dec ([Protocols]) and MUST
  stay in sync with the Core Unlock driver's own .dec declaration:
    gMeiMeiDXEv3CoreUnlockProtocolGuid =
      { 0x687319be, 0x5bc5, 0x44a7, { 0x8e, 0x60, 0xe5, 0xf1, 0xe5, 0xfd, 0x01, 0x68 } }
**/

#ifndef __MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL_H__
#define __MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL_H__

#include <Uefi.h>

typedef struct _MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL  MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL;

/**
  Read the learned factory core presence mask.

  The mask is re-read from the persisted factory record and validated by its
  CRC-32 fingerprint on every call, matching the driver's own learning check.
  Before the one-shot cold-boot learning cycle has persisted a record, the
  service returns EFI_NOT_FOUND.

  @param[in]  This          Protocol instance pointer.
  @param[out] FactoryMask   Receives the factory core presence mask low byte.

  @retval EFI_SUCCESS       The factory mask is available.
  @retval EFI_NOT_FOUND     No valid factory record yet (learning cold boot
                            pending).
  @retval others            The factory record could not be read.
**/
typedef
EFI_STATUS
(EFIAPI *MEIMEIDXEV3_GET_FACTORY_MASK)(
  IN  MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL  *This,
  OUT UINT8                              *FactoryMask
  );

struct _MEIMEIDXEV3_CORE_UNLOCK_PROTOCOL {
  MEIMEIDXEV3_GET_FACTORY_MASK  GetFactoryMask;
};

#endif
