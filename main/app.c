#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_log_internal.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "driver/gpio.h"

#include "sdkconfig.h"

#include "pn532.h"
#include "NFC_reader.h"


typedef unsigned char byte;
// #define BLINK_GPIO 2

#define PN532_SCK 2
#define PN532_MOSI 4
#define PN532_SS 32   // CONFIG_PN532_SS
#define PN532_MISO 35 // CONFIG_PN532_MISO

#define CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY 1

static const char *TAG = "APP";

static pn532_t nfc;

bool authenticated = false;





void nfc_task(void *pvParameter)
{
  TCardInfo Karta1;
  NFC_init(&nfc, 20, &Karta1,PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  NFC_LoadNFC(&nfc, &Karta1);
  
  while (1)
  {
   
    if(NFC_CheckStructIsSame(&nfc,&Karta1,1) != 0)
    {
      ESP_LOGI(TAG,"Hodnoty jsou jiné, zapisuji");

      NFC_WriteAndCheck(&nfc,&Karta1,1);
      
    }
    else
    {
      if(Karta1.sDataNFC[1].AA >= 0x10)
      {ESP_LOGI(TAG,"Hodnota je 10, nuluju");
        Karta1.sDataNFC[1].AA = 0x0;
      }
      else
      {
        ESP_LOGI(TAG,"Zvetsuji hodnotu o 1. Aktualní hodnota: %x",Karta1.sDataNFC[1].AA);
        Karta1.sDataNFC[1].AA = Karta1.sDataNFC[1].AA +1;
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  // configure board to read RFID tags
  
}

void app_main()
{
  xTaskCreate(&nfc_task, "nfc_task", 4096, NULL, 4, NULL);
}
