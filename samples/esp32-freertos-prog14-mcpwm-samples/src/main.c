#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>
#include "freertos/queue.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/mcpwm_prelude.h"
#define TWDT_TIMEOUT_MS 2000

#define TAG "mcpwm"

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void app_main(void){
  int groupid = 0;
  ESP_LOGI(TAG, "Create timer and operator");

  // タイマー作成
  // タイマーはresolution_hz、period_ticksの設定で
  // 1us毎に1カウントアップ、20ms(50Hz)を1周期、つまり20msでゼロクリアを繰り返す(0-20000)
  mcpwm_timer_handle_t timer = NULL;
  mcpwm_timer_config_t timer_config = {
    .group_id = groupid,
    .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000, // 1MHz, 1us per tick
    .period_ticks = 20000, // 20000 ticks, 20ms, 50Hz
    .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
  };
  esp_err_t ret = mcpwm_new_timer(&timer_config, &timer);

  while (1) {
    delay_ms(10);
  }
}
