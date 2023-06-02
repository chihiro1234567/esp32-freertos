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

#define TWDT_TIMEOUT_MS 2000

static const char *TAG = "test1";

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;

EventGroupHandle_t event_group = NULL;
#define EVENT_GPIO_A (1<<0)
#define EVENT_GPIO_B (1<<1)

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void task1(void *pvParameters) {
  ESP_LOGW(TAG, "==== task1 start ====");
  while (1) {
    ESP_LOGW(TAG, "wait event ...");

    // イベント発火後にクリアするかしないか？
    // pdFALSE: 2つボタン同時押し操作などの場合、押している間、ずっと何度も発火する
    // pdTRUE: 2つボタン同時押しの最初だけ発火する。もう一度発火させる場合は、両方とも離して再度押す
    uint32_t eBits = xEventGroupWaitBits(
      event_group,                 // イベントグループを指定
      EVENT_GPIO_A | EVENT_GPIO_B, // 一つ以上のイベントビットを指定
      pdTRUE,                      // 呼び出し後にイベントビットをクリアするか
      pdTRUE,                      // 指定したイベントビットがすべて揃うまで待つか
      portMAX_DELAY                // 待ち時間
    );
    ESP_LOGW(TAG, "** event task **");
    delay_ms(1);
  }
}

void task2(void *pvParameters) {
  ESP_LOGI(TAG, "==== task2 start ====");
  while (1) {
    //ESP_LOGI(TAG, "task4 wait...");
    delay_ms(1000);
  }
}

// イベントグループ
// ビット単位でミューテックスを管理して、最大24個まで
// 同時にミューテックスを利用できる。I2Cや無線系通信などで利用されている。
// 複数のミューテックスが取得できる場合のみに処理を行うケースなどで利用する
// イベントグループを使わずに複数ミューテックスをハンドリングしようとすると
// 不具合によってロックしてしまうので注意

// 一番わかりやすいケースとしては、
// 2つのボタンを同時押ししているときだけ何かするetc

// EventGroupHandle_t event_group = xEventGroupCreate() イベントグループの初期化
// xEventGroupClearBits(event_group, 0xFFFFFF); イベントグループの初期化

// 今回はGPIOの割込みを2個用意して、2個同時ONのときに特定タスク処理を実行してみる

// GPIO割込み
void IRAM_ATTR gpio_isr_handler(void *arg){
  uint32_t gpio_num = (uint32_t) arg;
  // ESP_LOG*は割込み内では？使えない。エラーになる。
  int level = gpio_get_level(gpio_num);
  esp_rom_printf("[interrupt!] GPIO=%lu, val=%d\n", gpio_num, level);
  
  if(level == 0){
    xEventGroupClearBits(event_group, gpio_num==GPIO_NUM_5 ? EVENT_GPIO_A:EVENT_GPIO_B);
  }else{
    xEventGroupSetBits(event_group, gpio_num==GPIO_NUM_5 ? EVENT_GPIO_A:EVENT_GPIO_B);
  }
}

// GPIO：割込み設定、プログラムでLOW=>HIGH
void gpio_trriger(){

  // https://github.com/espressif/esp-idf/issues/285
  // OUTPUTモード：割込み、プルアップ・ダウンは無効にする => つまり割込みとか使えない内部からのGPIOのHIGH/LOWってことかな？
  // INPUTモード：割込み、プルアップ・ダウンを指定する => 割込みとか使える。外部からの電圧変化の読み取り

  // 実際にやった組み合わせ
  // GPIO_MODE_INPUT, pull_down, GPIO_INTR_POSEDGE => 通常LOW、HIGHでトリガー(チャタリングしている)
  // GPIO_MODE_INPUT_OUTPUT, pull_down, GPIO_INTR_POSEDGE => 通常LOW、HIGHでトリガー(チャタリングしている)
  // GPIO_MODE_INPUT, pull_down, GPIO_INTR_NEGEDGE => 通常LOW、HIGHでトリガー(チャタリングしている)
  // GPIO_MODE_INPUT, pull_down, GPIO_INTR_HIGH_LEVEL => HIGHでトリガーするがずっと割込みしてWDT発動する

  // GPIO_MODE_INPUT, pull_up, GPIO_INTR_POSEDGE => 通常HIGH、LOWでトリガー(チャタリングしている)
  // GPIO_MODE_INPUT, pull_up, GPIO_INTR_NEGEDGE => 通常HIGH、LOWでトリガー(チャタリングしている)
  
  // GPIO_MODE_OUTPUT, pull_down, GPIO_INTR_POSEDGE => 通常LOW、外部からのHIGH/LOWどちらも反応しない

  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_INPUT, // GPIO_MODE_INPUT GPIO_MODE_OUTPUT GPIO_MODE_INPUT_OUTPUTにしないと、外部からPINのHIGH、LOWしても反応しない
    .pin_bit_mask = (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_7),
    .pull_down_en = 1,
    .pull_up_en = 0,
  };

  gpio_config(&io_conf);
  gpio_set_level(GPIO_NUM_5, 0);
  gpio_set_level(GPIO_NUM_7, 0);
  // https://esp32.com/viewtopic.php?t=1130
  // GPIO_INTR_POSEDGE GPIO_INTR_NEGEDGE GPIO_INTR_ANYEDGE GPIO_INTR_HIGH_LEVEL GPIO_INTR_LOW_LEVEL
  gpio_set_intr_type(GPIO_NUM_5, GPIO_INTR_POSEDGE);
  gpio_set_intr_type(GPIO_NUM_7, GPIO_INTR_POSEDGE);
  gpio_uninstall_isr_service();
  gpio_install_isr_service(0);

  gpio_isr_handler_add(GPIO_NUM_5, gpio_isr_handler, (void *)GPIO_NUM_5);
  gpio_isr_handler_add(GPIO_NUM_7, gpio_isr_handler, (void *)GPIO_NUM_7);
  
}

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

  // create & initialize event group
  event_group = xEventGroupCreate();
  xEventGroupClearBits(event_group, 0xFFFFFF);

  gpio_trriger(GPIO_NUM_5);
  gpio_trriger(GPIO_NUM_7);

  xTaskCreatePinnedToCore(task1, "task1", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);
  //xTaskCreatePinnedToCore(task2, "task2", 8192, NULL, 1, &taskHandle2, APP_CPU_NUM);

  ESP_LOGI(TAG, "<=== app_main end");
}
