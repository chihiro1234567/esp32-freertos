#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>
#include "freertos/queue.h"
#include "driver/timer.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
static const char *TAG = "test1";

#define TWDT_TIMEOUT_MS 2000

TaskHandle_t taskHandle;

void delay_ms(uint32_t ms)
{
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void app_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== app_task start ====");
  while (1) {
    ESP_LOGW(TAG, "notify wait....");
    uint32_t ulNotifiedValue;
    xTaskNotifyWait(0, 0, &ulNotifiedValue, portMAX_DELAY);
    ESP_LOGW(TAG, "@@ notify received @@");
  }
}

// Timer割込み
void IRAM_ATTR timer_isr_handler(void *arg){
  esp_rom_printf("\n");
  BaseType_t taskWoken = pdFALSE;
  xTaskNotifyFromISR(taskHandle, 0, eIncrement, &taskWoken);
  esp_rom_printf("[interrupt!] xTaskNotifyFromISR\n");
}

// https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/api-reference/peripherals/timer.html
// ESP32は2つのタイマーモジュールがあり、それぞれ2個の64ビットタイマーがある（合計4個）
// ESP32 タイマーグループは timer_group_t で識別する
// timer_config_tでタイマーの動作方法を定義して、timer_init()でタイマーを初期化
// githubのサンプル
// https://github.com/espressif/esp-idf/tree/v4.3/examples/peripherals/timer_group

// 周波数の話
// https://rt-net.jp/mobility/archives/10318
#define TIMER_DIVIDER (80)  // 分周比

// クロック数とカウント数の掛け算でタイマーの間隔を制御
// タイマーはAPB_CLK_FREQ = 80*1000000 = 80MHz間隔で動作している(ペリフェラル周波数)
// つまり80MHz(divider=1)の間隔でカウントしている

// タイマーは、timer_set_alarm_value()で指定した値をカウントすると発動する
// alarm_value=1だと、80MHzで1カウントするので、1/80000000=0.0000000125secで発動

#define TIMER_SCALE   (APB_CLK_FREQ / TIMER_DIVIDER)
// 分周比dividerを設定すると、たとえばdivider=80だと80MHz/80=1MHzでカウントするようになる
// alarm_value=1だと、1MHzで1カウントになるので、1/10000000=0.0000001=1usecで発動。だいぶわかりやすくなる。
// なのでalarm_value=10000000とすれば、1secで発動するようになる。
// TIMER_SCALE=10000000、1secを1スケールとしている。
// 0.5secなら 0.5 x TIMER_SCALE, 0.01sec => 0.01 x TIMER_SCALEでtimer_set_alarm_value()にセットするようにしている

static void example_tg_timer_init(int group, int timer, bool auto_reload, int timer_interval_sec)
{
  // timer setup
  timer_config_t config = {
    .divider = TIMER_DIVIDER, // 分周比16ビットの値(2 - 65536)
    .counter_dir = TIMER_COUNT_UP, //カウント方法
    .counter_en = TIMER_PAUSE, // init時にすぐ開始するか、否か
    .alarm_en = TIMER_ALARM_EN, // アラームON/OFF
    .auto_reload = auto_reload, // アラーム時に初期値をリロードするか
  };
  timer_init(group, timer, &config);

  // timerカウンタの初期値、auto_reload=trueの場合、リロード時もこの値になる
  timer_set_counter_value(group, timer, 0);
  // アラーム値
  timer_set_alarm_value(group, timer, timer_interval_sec * TIMER_SCALE);
  // 割込み有効
  timer_enable_intr(group, timer);
  // 割込み追加
  timer_isr_callback_add(group, timer, timer_isr_handler, NULL, 0);

  // timer開始
  timer_start(group, timer);
}

void app_main()
{
  ESP_LOGI(TAG, "app_main start ===>");
  ESP_ERROR_CHECK(esp_task_wdt_deinit());
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = TWDT_TIMEOUT_MS,
    .idle_core_mask = 1, // WDT core1とりあえず無効
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));
  xTaskCreatePinnedToCore(app_task, "app_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);

  // 3sec timer
  example_tg_timer_init(TIMER_GROUP_0, TIMER_0, true, 3);

  ESP_LOGI(TAG, "<=== app_main end");
}

