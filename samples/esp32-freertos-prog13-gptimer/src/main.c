#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>
#include "esp_log.h"

#include "freertos/queue.h"
#include "driver/gptimer.h"
#include <freertos/task.h>

#define TAG "test1"
#define TWDT_TIMEOUT_MS 2000

TaskHandle_t taskHandle;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

// https://github.com/espressif/esp-idf/blob/master/examples/system/sysview_tracing/main/sysview_tracing.c

// gptimerのコールバック
static bool timer_alarm_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
  bool need_yield = false;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  // タスクに通知する
  xTaskNotifyFromISR(taskHandle, 0, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken == pdTRUE) {
    need_yield = true;
  }
  return need_yield;
}

void timer_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== timer_task start ====");
  //-----------------------
  // Setup gptimer
  //-----------------------
  // resolution_hz,alarm_countについて
  // 旧タイマだとdividerとペリフェラル周波数(APB_CLK_FREQ = 80*1000000 = 80MHz)
  // から決定されていた値を直接resolution_hzに設定する感じ。以前もdivider=80として(80*1000000)/80=1000000にしていた
  // 実際のタイマーの時間は alarm_count / resolution_hz = alarm time[sec]

  gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,// GPTIMER_CLK_SRC_APB, GPTIMER_CLK_SRC_XTAL
    .direction = GPTIMER_COUNT_UP, // GPTIMER_COUNT_DOWN, GPTIMER_COUNT_UP
    .resolution_hz = 1000000, // 分解能
  };

  gptimer_handle_t gptimer = {0};
  esp_err_t ret = gptimer_new_timer(&timer_config, &gptimer);
  ESP_ERROR_CHECK(ret);

  // alarm_count=1000000=>1sec ok
  // alarm_count=10000=>10ms ok
  // alarm_count=1000=>1ms ok? ログ出すとWDT発動
  // alarm_count=100=>100us ok? ログ出すとWDT発動
  gptimer_alarm_config_t alarm_config = {
    .reload_count = 0, // auto_reload_on_alarm=trueのときだけ有効
    .alarm_count = 1000000, // タイマーの時間は alarm_count / resolution_hz = alarm time[sec]
    .flags.auto_reload_on_alarm = true, // 発動したら自動的に次のタイマーを設定するか否か
  };

  // コールバックの登録
  gptimer_event_callbacks_t callback = {
    .on_alarm = timer_alarm_callback,
  };

  ret = gptimer_register_event_callbacks(gptimer, &callback, NULL);
  ESP_ERROR_CHECK(ret);
  // アラームの設定
  ret = gptimer_set_alarm_action(gptimer, &alarm_config);
  ESP_ERROR_CHECK(ret);
  ret = gptimer_enable(gptimer);
  ESP_ERROR_CHECK(ret);
  // タイマー開始
  ret = gptimer_start(gptimer);
  ESP_ERROR_CHECK(ret);

  while(1){
    uint32_t ulNotifiedValue;
    //ESP_LOGI(TAG, "wait gptimer alarm ...");
    xTaskNotifyWait(0, 0, &ulNotifiedValue, portMAX_DELAY);
    ESP_LOGW(TAG, "alarm!");
    //delay_ms(1);
  }

}

void app_main(){

  esp_log_level_set("*", ESP_LOG_VERBOSE);

  // WDT
  ESP_LOGI(TAG, "app_main start ===>");
  ESP_ERROR_CHECK(esp_task_wdt_deinit());
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = TWDT_TIMEOUT_MS,
    .idle_core_mask = 3,
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));
  
  xTaskCreatePinnedToCore(timer_task, "timer_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);

  ESP_LOGI(TAG, "<=== app_main end");
}


