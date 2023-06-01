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

#define TWDT_TIMEOUT_MS 2000

static const char *TAG = "test1";

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;
QueueHandle_t xQueueHandle;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void queue_send_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== queue_send_task start ====");
  uint8_t data = 0;
  while (1) {
    data += 1;
    BaseType_t ret = xQueueSend(xQueueHandle, &data, 0);
    ESP_LOGI(TAG, "pdTRUE=%d, errQUEUE_FULL=%d, ret = %d, send = %d",pdTRUE, errQUEUE_FULL, ret , data);
    delay_ms(500);
  }
}
void queue_receive_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== queue_receive_task start ====");
  uint8_t data = 0;
  int r;

  while (1) {
    int ret = xQueueReceive(xQueueHandle, &data, 0);
    // 受信タイミングはランダム 100ms - 1000ms
    r = esp_random() % 10 + 1;
    ESP_LOGW(TAG, "rand=%d, ret=%d, receive = %d", r, ret, data);
    ESP_LOGW(TAG, "          stack = %d", uxQueueMessagesWaiting(xQueueHandle));
    ESP_LOGW(TAG, "      available = %d", uxQueueSpacesAvailable(xQueueHandle));
    delay_ms((int)(r*100));
  }
}
// キューについて
// Create キューの入れ物作成
//   xQueueCreate(length, size)成功するとQueueHandle_t型のハンドルが返る
// Write キュー送信
//   xQueueSend(queueSEND_TO_BACK)
//   xQueueSendToBack(queueSEND_TO_BACK)
//   xQueueSendToFront(queueSEND_TO_FRONT)
//   xQueueOverwrite(queueOVERWRITE) xTicksToWait=0 キューが満杯状態でも最後の値を上書きする(xQueueSendToBack()の上書きバージョン)

// Write系の引数
// QueueHandle_t xQueue, キューのハンドル
// const void * pvItemToQueue, 書き込むキューデータ
// TickType_t xTicksToWait, キューが満杯のときの待ち時間(Tickカウント) ms / portTICK_PERIOD_MS
//    #define portTICK_PERIOD_MS ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
//    configTICK_RATE_HZ=1000にしていれば、100msであれば、100でOKだが、念のため ms/portTICK_PERIOD_MSで指定する
// BaseType_t xCopyPosition キューの書き込み位置（一般的にはFIFOなのでqueueSEND_TO_BACKが妥当)
//    どの関数もPositionが決まっていて関数名に追加されている

// Receive キュー受信
// Wait キュー待ち数確認
// Reset キュークリア
// Peek キューを取り出さずに受信する ※使わない
// Delete キュー削除 ※使わない
// 割込み関数内で使う*FromISR版もある

void app_main(){
  ESP_LOGI(TAG, "app_main start ===>");
  ESP_ERROR_CHECK(esp_task_wdt_deinit());
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = TWDT_TIMEOUT_MS,
    .idle_core_mask = 1, // WDT core1とりあえず無効
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

  // create queue
  xQueueHandle = xQueueCreate(5, sizeof(uint8_t));
  
  xTaskCreatePinnedToCore(queue_receive_task, "queue_receive_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);
  xTaskCreatePinnedToCore(queue_send_task, "queue_send_task", 8192, NULL, 1, &taskHandle2, APP_CPU_NUM);

  ESP_LOGI(TAG, "<=== app_main end");
}
