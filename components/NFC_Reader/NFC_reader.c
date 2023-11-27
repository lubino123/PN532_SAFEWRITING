#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include <esp_log.h>
#include "sdkconfig.h"

#include "NFC_reader.h"
#include "pn532.h"

#define OFFSETDATA 8
#define PAGESIZE 4
#define MAXERRORREADING 5
#define TIMEOUTCHECKCARD 200

#define NFC_READER_ALL_DEBUG_EN 0
#define NFC_READER_DEBUG_EN 1

#ifdef NFC_READER_ALL_DEBUG_EN
#define NFC_READER_ALL_DEBUG(tag, fmt, ...)                      \
  do                                                             \
  {                                                              \
    if (tag && *tag)                                             \
    {                                                            \
      printf("\x1B[31m[%s]DA:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
    }                                                            \
    else                                                         \
    {                                                            \
      printf(fmt, ##__VA_ARGS__);                                \
    }                                                            \
  } while (0)
#else
#define NFC_READER_ALL_DEBUG(fmt, ...)
#endif

#ifdef NFC_READER_DEBUG_EN
#define NFC_READER_DEBUG(tag, fmt, ...)                         \
  do                                                            \
  {                                                             \
    if (tag && *tag)                                            \
    {                                                           \
      printf("\x1B[36m[%s]D:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
    }                                                           \
    else                                                        \
    {                                                           \
      printf(fmt, ##__VA_ARGS__);                               \
    }                                                           \
  } while (0)
#else
#define NFC_READER_DEBUG(fmt, ...)
#endif

#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)

/**************************************************************************/
/*!
    @brief  Inicializace PN532 desky a vytvoření pole struktur TDataNFC, podle velikosti NFC Čipu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCapacity Velikost čipu v Bytech
    @param  aCardInfo Pointer na strukturu CardInfo
    @param  clk       CLK GPIO Výstup
    @param  miso      MISO GPIO Výstup
    @param  mosi      MOSI GPIO Výstup
    @param  ss        SS GPIO Výstup

    @returns Hodnota jestli se Deska PN53X našla a nastavila
*/
/**************************************************************************/
bool NFC_init(pn532_t *aNFC, size_t aCapacity, TCardInfo *aCardInfo, uint8_t aClk, uint8_t aMiso, uint8_t aMosi, uint8_t aSs)
{
  static const char *TAGin = "NFC_init";
  NFC_READER_DEBUG(TAGin, "Inicializuji kartu:\n");
  aCardInfo->sSize = aCapacity;
  NFC_READER_ALL_DEBUG(TAGin, "Velikost pameti je %zu. \n", aCardInfo->sSize);
  pn532_spi_init(aNFC, aClk, aMiso, aMosi, aSs);
  pn532_begin(aNFC);

  size_t NumOfBlocks = aCapacity / TDataNFC_Size;
  aCardInfo->sNumOfBlocks = NumOfBlocks;
  (aCardInfo->sDataNFC) = (TDataNFC *)malloc(TDataNFC_Size * NumOfBlocks);

  uint32_t versiondata = pn532_getFirmwareVersion(aNFC);
  if (!versiondata)
  {
    NFC_READER_DEBUG(TAGin, "Nelze najít PN53x desku.\n");
    return false;
  }
  // Got ok data, print it out!
  NFC_READER_DEBUG(TAGin, "Našla se deska PN5 %lu.\n", (versiondata >> 24) & 0xFF);
  NFC_READER_ALL_DEBUG(TAGin, "Firmware ver. %lu.%lu. \n", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
  pn532_SAMConfig(aNFC);
  return true;
}

/**************************************************************************/
/*!
    @brief  Zaplnění TDataNFC struktury daty z NFC čipu

    @param  aNFC      Pointer na NFC
    @param  aDataNFC  Pointer na pole TDataNFC struktur
    @param  anumOfNFCStruct       Číslo struktury, kterou chceme načíst z NFC Čipu

    @returns Chybový kód(0 - Nacteno správně, 1 - Nelze číst z MifareUltralight čipu)//TODO
*/
/**************************************************************************/
uint8_t NFC_GetStructData(pn532_t *aNFC, TDataNFC *aDataNFC, uint16_t anumOfNFCStruct)
{
  static const char *TAGin = "NFC_GetStructData";
  NFC_READER_DEBUG(TAGin, "Cekam na kartu ISO14443A Card: ");
  fflush(stdout);
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t uidLength;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, uid, &uidLength, 0);
  NFC_READER_DEBUG("", "Karta Prilozena\n");
  if (success)
  {

    if (uidLength == 4)
    {
      NFC_READER_ALL_DEBUG(TAGin, "Jedná se o Mifare Classic kartu (4 byte UID)");

      // Now we need to try to authenticate it for read/write access
      // Try with the factory default KeyA: 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
      ESP_LOGI(TAGin, "Trying to authenticate block 4 with default KEYA value");
      uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

      success = pn532_mifareclassic_AuthenticateBlock(aNFC, uid, uidLength, 4, 0, keya);

      if (success)
      {
        ESP_LOGI(TAGin, "Sector 1 (Blocks 4..7) has been authenticated");
        uint8_t data[16];

        success = pn532_mifareclassic_ReadDataBlock(aNFC, 4, data);
        if (success)
        {
          // Data seems to have been read ... spit it out
          ESP_LOGI(TAGin, "Reading Block 4:");
          for (int i = 0; i < 16; ++i)
          {
            ESP_LOGI(TAGin, "%d", data[i]);
          }

          // Wait a bit before reading the card again
          // vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        else
        {
          ESP_LOGI(TAGin, "Ooops ... unable to read the requested block.  Try another key?");
          return 2; // Cannot read requested block
        }
      }
      else
      {
        ESP_LOGI(TAGin, "Ooops ... authentication failed: Try another key?");
        return 3;
      }
    }
    else if (uidLength == 7)
    {
      // We probably have a Mifare Ultralight card ...
      NFC_READER_ALL_DEBUG(TAGin, "Jedná se o Mifare Ultralight tag (7 byte UID)\n");
      // ESP_LOGI(TAG, "Reading page ");
      // ESP_LOGI(TAG,  "%d\n",i );

      uint8_t data[TDataNFC_Size];
      success = pn532_mifareultralight_ReadPage(aNFC, ((TDataNFC_Size * anumOfNFCStruct) / PAGESIZE) + OFFSETDATA, data);
      if (success)
      {
        NFC_READER_ALL_DEBUG(TAGin, "Sektor: %X:    ", ((TDataNFC_Size * anumOfNFCStruct) / PAGESIZE) + OFFSETDATA);
        // Data seems to have been read ... spit it out
        for (int j = 0; j < TDataNFC_Size; ++j)
        {
          NFC_READER_ALL_DEBUG("", "%d:%x  ", j, data[j + (TDataNFC_Size * anumOfNFCStruct) % PAGESIZE]);
          ((uint8_t *)aDataNFC)[j] = data[j + (TDataNFC_Size * anumOfNFCStruct) % PAGESIZE];
        }
        NFC_READER_ALL_DEBUG("", "\n");
      }
      else
      {
        NFC_READER_DEBUG(TAGin, "Nelze cist z karty!\n");
        return 1;
      }

      NFC_READER_DEBUG(TAGin, "Nacteno!\n");

      return 0;
    }
  }
  else
  {
    // PN532 probably timed out waiting for a card
    NFC_READER_DEBUG(TAGin, "Vyprsel cas cekani na kartu");
    return 5;
  }
  return 0;
}

/**************************************************************************/
/*!
    @brief  Načteni Cele editovatelné části do NFC Čipu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu

    @returns True - Pokud se Data načetla
*/
/**************************************************************************/
bool NFC_LoadNFC(pn532_t *aNFC, TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_LoadNFC";
  size_t errorCounter = 0;
  NFC_READER_DEBUG(TAGin, "Nacitam vsechny data z karty\n");
  uint8_t iUid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t iUidLength;
  NFC_getUID(aNFC,iUid,&iUidLength);
  NFC_saveUID(aCardInfo,iUid,iUidLength);
  TDataNFC idataNFC1;
  for (size_t i = 0; i < aCardInfo->sNumOfBlocks; ++i)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Nacítam data z %d stranky: \n", i);

    if (NFC_GetStructData(aNFC, &idataNFC1, i) != 0)
    {
      ++errorCounter;
      if (errorCounter == MAXERRORREADING)
      { // NFC_READER_DEBUG("",MAXERRORREADING );
        NFC_READER_DEBUG(TAGin, STRINGIFY(MAXERRORREADING) "x se nepodarilo nacist hodnotu.\n");
        return false;
      }
      else
      {
        --i;
        continue;
      }
    }
    errorCounter = 0;
    aCardInfo->sDataNFC[i] = idataNFC1;

    NFC_READER_ALL_DEBUG(TAGin, "Nactena data: ");
    for (size_t j = 0; j < TDataNFC_Size; ++j)
    {
      NFC_READER_ALL_DEBUG("", "%x ", ((uint8_t *)&idataNFC1)[j]);
    }
    NFC_READER_ALL_DEBUG("", "\n\n");

    // NFC_READER_ALL_DEBUG(TAGin, "Nactena Data %d: %x %x %x %x", i, idataNFC1, ((uint8_t *)&idataNFC1)[1], ((uint8_t *)&idataNFC1)[2], ((uint8_t *)&idataNFC1)[3]);
  }
  return true;
}

/**************************************************************************/
/*!
    @brief  Vytiskne celé pole TDataNFC struktur

    @param  aCardInfo Pointer na TCardInfo strukturu

*/
/**************************************************************************/
void NFC_PrintData(TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_PrintData";
  for (size_t i = 0; i < aCardInfo->sNumOfBlocks; ++i)
  {
    char Readed[TDataNFC_Size + 1];
    for (size_t j = 0; i < TDataNFC_Size; ++i)
    {
      Readed[j] = ((uint8_t *)aCardInfo->sDataNFC + i * TDataNFC_Size)[j];
    }
    Readed[TDataNFC_Size] = '\0';
    NFC_READER_DEBUG(TAGin, "Data %d: %s\n", i, Readed);
  }
}

/**************************************************************************/
/*!
    @brief  Zkontroluje jestli struktura TDataNFC je stejná v zařízení jak v NFC čipu

    @param  aNFC      Pointer na NFC
    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  anumOfNFCStruct Číslo indexu kontrolované TDataNFC Struktury

    @returns 0 - Pokud se Data načetla, 1 - Pokud se struktura liší, 2 - Pokud je anumOfNFCStruct mimo rozsah,3 - Nelze cist z karty
*/
/**************************************************************************/
uint8_t NFC_CheckStructIsSame(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct)
{
  static const char *TAGin = "NFC_CheckStructIsSame";
  if (anumOfNFCStruct <= aCardInfo->sNumOfBlocks)
  {
    TDataNFC idataNFC1;
    NFC_READER_ALL_DEBUG(TAGin, "Porovnavam data\n");
    if (NFC_GetStructData(aNFC, &idataNFC1, anumOfNFCStruct) == 0)
    {
      for (size_t i = 0; i < TDataNFC_Size; ++i)
      {
        if (((uint8_t *)aCardInfo->sDataNFC + anumOfNFCStruct * TDataNFC_Size)[i] != ((uint8_t *)&idataNFC1)[i])
        {
          NFC_READER_ALL_DEBUG(TAGin, "Struktura se liší na %d pozici, v Zařízení: %x na NFC karte: %x\n", i, ((uint8_t *)aCardInfo->sDataNFC + anumOfNFCStruct * TDataNFC_Size)[i], ((uint8_t *)&idataNFC1)[i]);
          return 1;
        }
      }
      NFC_READER_ALL_DEBUG(TAGin, "Struktury jsou stejné\n");
      return 0;
    }
    else
    {
      return 3;
    }
  }
  else
  {
    return 2;
  }
}

/**************************************************************************/
/*!
    @brief  Zapíše strukturu TDataNFC na NFC Čip

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  anumOfNFCStruct Index struktury TDataNFC, která se má zapsat

    @returns 0 - Pokud se podařilo zapsat, 1 - Pokud je anumOfNFCStruct mimo rozsah karty

*/
/**************************************************************************/
uint8_t NFC_WriteStruct(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct)
{
  static const char *TAGin = "NFC_WriteStruct";
  if (aCardInfo->sNumOfBlocks >= anumOfNFCStruct)
  {
    size_t iWritingPointer = anumOfNFCStruct * TDataNFC_Size;
    uint8_t iData[PAGESIZE];

    uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
    uint8_t iuidLength;

    size_t iBlockWriting = iWritingPointer / PAGESIZE;
    int iPointerToWrite = iWritingPointer % PAGESIZE;
    size_t iPage = 0;
    NFC_READER_ALL_DEBUG(TAGin, "Zapisuji na kartu\n");
    NFC_READER_ALL_DEBUG(TAGin, "Zapisuji strukturu %d, iPointer: %d, datasize: %zu\n", anumOfNFCStruct, iPointerToWrite, TDataNFC_Size);
    while (iPointerToWrite > -(int)TDataNFC_Size)
    {
      NFC_READER_ALL_DEBUG(TAGin, "Zapisuji do bloku: %d\n", iBlockWriting + OFFSETDATA + iPage);
      for (size_t i = 0; i < PAGESIZE; ++i)
      {
        if (iPointerToWrite > 0)
        {
          iData[i] = *(((uint8_t *)aCardInfo->sDataNFC + anumOfNFCStruct * TDataNFC_Size) - iPointerToWrite);
          --iPointerToWrite;
        }
        else
        {
          iData[i] = *(((uint8_t *)aCardInfo->sDataNFC + anumOfNFCStruct * TDataNFC_Size) - iPointerToWrite);
          --iPointerToWrite;
        }
      }
      uint8_t isuccess = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, 0);
      if (isuccess == 1)
      {
        if (iuidLength == 4)
        { // TO-DO
        }
        else if ((iuidLength == 7))
        {
          isuccess = pn532_mifareultralight_WritePage(aNFC, iBlockWriting + OFFSETDATA + iPage, iData);
          NFC_READER_ALL_DEBUG(TAGin, "Zapsano na %d stranu\n", iBlockWriting + OFFSETDATA + iPage);
        }
      }
      ++iPage;
    }
    return 0;
  }
  else
  {
    return 1;
  }
}
/**************************************************************************/
/*!
    @brief  Funkce Odalokuje již alokovanou pamět

    @param   aCardInfo      Pointer na TCardInfo strukturu, která má být odalokována.
    @returns    0 - Paměť se v pořádku odalokovala, 1 - TDataNFC je již NULL, 2 - TCardInfo je již NULL
*/
/**************************************************************************/
uint8_t NFC_DeAlloc(TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_DeAlloc";
  NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam TDataNFC\n");
  if (aCardInfo->sDataNFC == NULL)
  {
    NFC_READER_ALL_DEBUG(TAGin, "TDataNFC je již null\n");
    return 1;
  }
  free(aCardInfo->sDataNFC);
  aCardInfo->sDataNFC = NULL;
  NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam TCardInfo\n");
  if (aCardInfo->sDataNFC == NULL)
  {
    NFC_READER_ALL_DEBUG(TAGin, "TCardInfo je již null\n");
    return 2;
  }
  free(aCardInfo);
  aCardInfo = NULL;
  return 0;
}

/**************************************************************************/
/*!
    @brief  Zapíše strukturu a zkontroluje

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  anumOfNFCStruct Index struktury TDataNFC, která se má zapsat a ověřit

    @returns    0 - Hodnoty na kartě sedí se zapsanými, 1- Data se liší, 2 - Index anumOfNFCStruct je mimo rozsah struktury, 3 - Nelze cist z karty
*/
/**************************************************************************/

uint8_t NFC_WriteAndCheck(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t anumOfNFCStruct)
{
  static const char *TAGin = "NFC_WriteAndCheck";
  uint8_t iZapis = NFC_WriteStruct(aNFC, aCardInfo, anumOfNFCStruct);
  if (iZapis != 0)
  {
    NFC_READER_DEBUG(TAGin, "Index struktury TDataNFC je mimo rozsah.\n");
    return 2;
  }
  switch (NFC_CheckStructIsSame(aNFC, aCardInfo, anumOfNFCStruct))
  {
  case 0:
    NFC_READER_DEBUG(TAGin, "Data se správně nahrála.\n");
    return 0;
    break;
  case 1:
    NFC_READER_DEBUG(TAGin, "Data se liší.\n");
    return 1;
    break;
  case 2:
    NFC_READER_DEBUG(TAGin, "Index struktury TDataNFC je mimo rozsah.\n");
    return 2;
    break;
  case 3:
    NFC_READER_DEBUG(TAGin, "Nelze cist z karty.\n");
    return 3;
    break;
  default:
    return 10;
    break;
  }
}

/**************************************************************************/
/*!
    @brief  OVěří jestli je karta přítomna na čtečce

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu

    @returns    true - Pokud je přítomna, false - Pokud neni přitomna
*/
/**************************************************************************/
bool NFC_isCardReadyToRead(pn532_t *aNFC)
{
  static const char *TAGin = "NFC_isCardReadyToRead";
  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t iuidLength;
  NFC_READER_ALL_DEBUG(TAGin, "Zkousím jestli je karta přítomna.\n");
  bool iStatus = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, TIMEOUTCHECKCARD);
  if (iStatus)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Neni pritomna.\n");
  }
  else
  {
    NFC_READER_ALL_DEBUG(TAGin, "Je pritomna.\n");
  }
  return iStatus;
}
/**************************************************************************/
/*!
    @brief  Získá UID a délku UID karty

    @param  aNFC      Pointer na NFC strukturu
    @param  aUid      Pointer na Uid pole
     @param  aUidLength      Pointer na Uid délku

    @returns    true - Pokud se správně načetlo, false - Pokud se špatně načetlo
*/
/**************************************************************************/
bool NFC_getUID(pn532_t *aNFC, uint8_t *aUid, uint8_t *aUidLength )
{
  static const char *TAGin = "NFC_getUID";
  NFC_READER_ALL_DEBUG(TAGin, "Ziskavam UID.\n");
  bool iSuccess = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, aUid, aUidLength, 0);
  if (!iSuccess)
    return false;
  NFC_READER_ALL_DEBUG(TAGin, "UID se nacetlo: ");
  for (size_t i = 0; i < *aUidLength; i++)
  {
    NFC_READER_ALL_DEBUG("", "%x ", aUid[i]);
  }
  NFC_READER_ALL_DEBUG("", ", s delkou: %zu. \n",*aUidLength);
  return true;
}
/**************************************************************************/
/*!
    @brief  Uloží UID a délku UID do TCardInfo struktury 

    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  aUid      Pointer na Uid pole
     @param  aUidLength      Délka UID

    @returns    true - Pokud se správně uložilo, false - Pokud se stala chyba při ukládání
*/
/**************************************************************************/
bool NFC_saveUID(TCardInfo *aCardInfo,uint8_t *aUid, uint8_t aUidLength )
{
  static const char *TAGin = "NFC_saveUID";
  NFC_READER_ALL_DEBUG(TAGin, "Ukladam UID:");
  for (size_t i = 0; i < aUidLength; i++)
  {
    aCardInfo->sUid[i] = aUid[i];
    NFC_READER_ALL_DEBUG("", "%x ", aCardInfo->sUid[i]);
  }
  NFC_READER_ALL_DEBUG("", ", s delkou: %zu. \n",aUidLength);
  aCardInfo->sUidLength = aUidLength;
  return true;
}