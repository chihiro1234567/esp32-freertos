#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>
#include <Arduino.h>

//https://github.com/espressif/esp-idf/blob/master/examples/system/task_watchdog/main/task_watchdog_example_main.c

// arduino core for esp32
//https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-misc.c

#define TWDT_TIMEOUT_MS 3000

void delay_ms(uint32_t ms)
{
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void task1(void *pvParameters) {
  while (1) {
    //printf("11111111111111111111111111111111111\n");

    delay_ms(1); // 結局のところ、delay入れればWDTがリセットされている状況
    //esp_task_wdt_reset(); // これだけ指定してもWDTは発火してしまう
  }
}
void task2(void *pvParameters) {
  while (1) {
    // task2 => APP_CPUで動かしているが、こちらはWDTがOFFなので、delayしてなくてもWDTが発火しない
  }
}

void app_main()
{
  //デフォルトでCore0 => ON, Core1 => OFF
  //基本変更しないほうがいい。
  enableCore0WDT();
  disableCore1WDT();

  xTaskCreatePinnedToCore(task1, "task1", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(task2, "task2", 8192, NULL, 1, NULL, APP_CPU_NUM);
    
}
