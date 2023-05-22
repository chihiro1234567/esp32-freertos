#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <Arduino.h>

#define BLINK_GPIO 1
#define BLINK_INTERVAL_MS 500

TaskHandle_t task1Handle = NULL;

void task1(void *pvParameter){
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
  uint32_t level = 0;
  while(1) {
    vTaskDelay(BLINK_INTERVAL_MS / portTICK_PERIOD_MS);
    gpio_set_level(BLINK_GPIO, level);
    level ^= 1; 
  }
}
void app_main() {
  // https://lang-ship.com/reference/unofficial/M5StickC/Functions/freertos/task/

  //xTaskCreate(&task1, "task1", 8192, NULL, 5, NULL); // #0コア内部指定
  //xTaskCreatePinnedToCore(task1, "task1", 8192, NULL, 5, &task1Handle, 1); // #1コア指定
  xTaskCreateUniversal(task1, "task1", 8192, NULL, 5, &task1Handle, CONFIG_ARDUINO_RUNNING_CORE);
}
