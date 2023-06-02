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

#define TWDT_TIMEOUT_MS 2000

static const char *TAG = "test1";

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;
volatile SemaphoreHandle_t semaphore;
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;

volatile uint8_t test_value = 0;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void task1(void *pvParameters) {
  ESP_LOGW(TAG, "==== task1 start ====");
  while (1) {
    ESP_LOGI(TAG, "blocked....");
    xSemaphoreTake(semaphore, portMAX_DELAY);
    ESP_LOGI(TAG, "took semaphore!");
    delay_ms(1);
  }
}
void task2(void *pvParameters) {
  ESP_LOGW(TAG, "==== task2 start ====");
  int r;
  while (1) {
    ESP_LOGW(TAG, "give semaphore");
    xSemaphoreGive(semaphore);
    r = esp_random() % 10 + 1;
    delay_ms((int)(r*100));
  }
}
void task3(void *pvParameters) {
  ESP_LOGW(TAG, "==== task3 start ====");
  while (1) {
    ESP_LOGW(TAG, "test_value=%u, task3 wait...", test_value);
    portENTER_CRITICAL(&mutex);
    test_value = rand()%10;
    delay_ms(500);
    portEXIT_CRITICAL(&mutex);
  }
}

void task4(void *pvParameters) {
  ESP_LOGI(TAG, "==== task4 start ====");
  while (1) {
    ESP_LOGI(TAG, "task4 wait...");
    portENTER_CRITICAL(&mutex);
    test_value = 0;
    delay_ms(1000);
    portEXIT_CRITICAL(&mutex);
  }
}

// セマフォとミューテックス

// セマフォは許可を与える機能
// 同時アクセスの上限を設定し、上限に達していた場合には利用ができないようにする
// 上限1の場合には、同時に一人しかリソース利用できないようにできる
// ミューテックスは、逆に使用者を決めて、他の人の利用を制限するイメージ
// 上限1だと、セマフォと、ミューテックスは結果的に同じ作用になる
// 実装方法はキューとほぼ同じ

// バイナリセマフォとカウンティングセマフォ
// バイナリセマフォ:上限1個のキューのようなもの
// カウンティングセマフォ:上限2個以上のキューのようなもの

// pio run -e esp32s3box -t upload

void app_main(){
  ESP_LOGI(TAG, "app_main start ===>");
  ESP_ERROR_CHECK(esp_task_wdt_deinit());
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = TWDT_TIMEOUT_MS,
    .idle_core_mask = 1, // WDT core1とりあえず無効
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

#if false
  // バイナリセマフォ
  //semaphore = xSemaphoreCreateBinary();
  // カウンティングセマフォ、初期値を10とすると、10Give分詰まった状態でスタートできる。
  semaphore = xSemaphoreCreateCounting(10,10);// 最大個数、初期値
  xTaskCreatePinnedToCore(task1, "task1", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);
  xTaskCreatePinnedToCore(task2, "task2", 8192, NULL, 1, &taskHandle2, APP_CPU_NUM);
#else
  // ミューテックス
  // task3、task4内でお互いに同じ値を編集する
  // 編集する際にロック/ロック解除して、自分が編集するときに排他制御する
  // タスク内で同じ値(test_value)を編集する場合はvolatile宣言する
  xTaskCreatePinnedToCore(task3, "task3", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);
  xTaskCreatePinnedToCore(task4, "task4", 8192, NULL, 1, &taskHandle2, APP_CPU_NUM);
#endif

  ESP_LOGI(TAG, "<=== app_main end");
}
