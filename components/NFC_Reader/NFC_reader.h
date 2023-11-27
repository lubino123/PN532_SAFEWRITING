/* ==========================================
    NFC_reader - Knihovna na synchronizaci dat z NFC Čipu
    Copyright (c) 2023 Luboš Chmelař
    [Licence]
========================================== */
#ifndef NFC_reader_H
#define NFC_reader_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "pn532.h"

  typedef struct __attribute__((packed))
  {
    uint8_t AA;
    uint8_t BB;
    uint8_t CC;
    uint8_t DD;
    uint8_t EE;
  } TDataNFC;

  typedef struct
  {
    size_t sSize;
    size_t sNumOfBlocks;
    TDataNFC *sDataNFC;
    uint8_t sUid[7];
    uint8_t sUidLength;

  } TCardInfo;

  static const size_t TDataNFC_Size = sizeof(TDataNFC);

  bool NFC_init(pn532_t *aNFC, size_t aCapacity, TCardInfo *aCardInfo, uint8_t aClk, uint8_t aMiso, uint8_t aMosi, uint8_t aSs);
  uint8_t NFC_DeAlloc(TCardInfo *aCardInfo);
  uint8_t NFC_GetStructData(pn532_t *aNFC, TDataNFC *aDataNFC, uint16_t anumOfNFCStruct);
  bool NFC_LoadNFC(pn532_t *aNFC, TCardInfo *aCardInfo);
  void NFC_PrintData(TCardInfo *aCardInfo);
  uint8_t NFC_CheckStructIsSame(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct);
  uint8_t NFC_WriteStruct(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct);
  uint8_t NFC_WriteAndCheck(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct);
  bool NFC_isCardReadyToRead(pn532_t *aNFC);
  bool NFC_getUID(pn532_t *aNFC, uint8_t *aUid, uint8_t *aUidLength);
  bool NFC_saveUID(TCardInfo *aCardInfo, uint8_t *aUid, uint8_t aUidLength);








#ifdef __cplusplus
}
#endif

#endif