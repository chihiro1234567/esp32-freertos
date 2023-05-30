#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
static const char *TAG = "test1";

#define TWDT_TIMEOUT_MS 2000

static esp_task_wdt_user_handle_t func_a_twdt_user_hdl;

static void func_a(void)
{
  // esp_task_wdt_add_user()で登録した場合、そのハンドルを使って
  // esp_task_wdt_reset_user()でリセットしないとWDTが発動する

  //E (5299) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
  //E (5299) task_wdt:  - func_a

  esp_task_wdt_reset_user(func_a_twdt_user_hdl);
}

void delay_ms(uint32_t ms)
{
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void task1(void *pvParameters) {
  // examples\system\task_watchdog\main\task_watchdog_example_main.c
  // WDTがONの場合、タスク内でWDTリセット(esp_task_wdt_reset)を制限時間内でする必要がある
  // リセットするためにはタスクの先頭で自身を追加(esp_task_wdt_add)しないと
  // リセット時に「登録されてない」と言われる。
  esp_task_wdt_add(NULL);

  // 別途WDTに追加登録することができる
  esp_task_wdt_add_user("func_a", &func_a_twdt_user_hdl);

  while (1) {
    ESP_LOGI(TAG,"11111");
    esp_task_wdt_reset();
    func_a();
    delay_ms(100);
  }
}
void task2(void *pvParameters) {
  esp_task_wdt_add(NULL);
  while (1) {
    ESP_LOGI(TAG, "22222");
    esp_task_wdt_reset();
    delay_ms(1000);
  }
}

// delayについて
// WDTが有効になっている状態でリセットするとWDTが発動しないと思っているが、
// かといって、10ms未満のdelayだとリセットしても発動してしまう
// delay_ms(10)として10ms以上のdelayがあると、リセットしなくてもWDTが発動しない
// サンプルの場合、ループ内でdelayしかしてないので、CPU負荷が高くなりすぎているのが問題だと思われる
// task3のようにWDTが有効なコアで実行する場合、
// esp_task_wdt_add => esp_task_wdt_resetは必要だと思われる

// delay=2msしているのにWDT発動してしまうのはなぜ？
// https://github.com/espressif/esp-idf/issues/1646
// 内部的にWDTを監視するタスクがいて、このタスクが十分にリソースを確保できないと
// 発動してしまうのは仕方がない。デフォルトのティック レート（CONFIG_FREERTOS_HZ）は100Hzで1000Hzに変更すると
// 2msでもOKになるらしい。※このプロジェクトだとCONFIG_FREERTOS_HZ=100

// ESP32マウスPart.33　ESP-IDFで1 msec delay を作る方法
// https://rt-net.jp/mobility/archives/10112

void task3(void *pvParameters) {
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  while (1) {
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    delay_ms(1);
  }
}
void task4(void *pvParameters) {
  while (1) {
    delay_ms(10);
  }
}

void app_main()
{
  // 本プログラムはArduino core for ESP32使用してないプログラムになる
  // 基本的にはAPIを使って手順を踏む必要がある

  //sdkconfigでWDTどうするかの設定がされているデフォルトだと両方ともON
  // 基本には触らない
  // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
  // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
  // rtosの内部でapp_main()をタスク実行する際にWDTを設定している
  // components/freertos/app_startup.c
  //#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
  //  twdt_config.idle_core_mask |= (1 << 0);
  //#endif
  //#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
  //    twdt_config.idle_core_mask |= (1 << 1);
  //#endif
  //ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

  // WDTを停止する場合コア毎にハンドルを渡して停止する。
  // リファレンスを見るとタスク内でesp_task_wdt_delete(NULL)で解除できるようなことが書いてあるが
  // 実際にはArduino core for esp32のdisableCore0WDT()だと、以下のように各コアのタスクハンドルを取ってきて指定する
  // 停止すれば、タスク内でリセットしなくてもWDTは発動しない。
  //TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
  //TaskHandle_t idle_1 = xTaskGetIdleTaskHandleForCPU(1);
  //esp_task_wdt_delete(idle_0);
  //esp_task_wdt_delete(idle_1);

  // 既にinitされているので一旦解除しないとエラーになる
  ESP_ERROR_CHECK(esp_task_wdt_deinit());

  // WDTを再度定義する
  // idle_core_maskでコア毎に有効・無効をビットで設定する
  // idle_core_mask=0 :(0b00) コア0無効、コア1無効
  // idle_core_mask=1 :(0b01) コア0有効、コア1無効
  // idle_core_mask=2 :(0b10) コア0無効、コア1有効
  // idle_core_mask=3 :(0b11) コア0有効、コア1有効
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = TWDT_TIMEOUT_MS,
    .idle_core_mask = 3,
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

  //xTaskCreatePinnedToCore(task1, "task1", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  //xTaskCreatePinnedToCore(task2, "task2", 8192, NULL, 1, NULL, APP_CPU_NUM);

  xTaskCreatePinnedToCore(task3, "task3", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(task4, "task4", 8192, NULL, 1, NULL, APP_CPU_NUM);
}
