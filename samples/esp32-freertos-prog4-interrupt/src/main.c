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

void delay_ms(uint32_t ms)
{
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void pro_task(void *pvParameters) {
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  while (1) {
    ESP_LOGI(TAG, "pro_task");
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    delay_ms(5);
  }
}
void app_task(void *pvParameters) {
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  while (1) {
    ESP_LOGI(TAG, "app_task");
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    delay_ms(5);
  }
}

// ISR (割り込みサービスルーチン)
// https://qiita.com/azuki_bar/items/7f31eb80e8b6b2eff17f
// ISRから使うものには"FromISR"が関数名に付与されている
// 割込みサービスルーチンで IRAM_ATTR と宣言すると、コンパイルされたコードは、
// ESP32の内部RAM(IEAM)に配置されます。これをしないとコードはフラッシュメモリに配置され、割込み時の処理が遅くなります
// ISRの関数にはIRAM_ATTR属性を必ずつける
// ISRの関数からアクセスする変数にはvolatile修飾子を必ずつける
// https://esp32.com/viewtopic.php?t=4978

void IRAM_ATTR gpio_isr_edge_handler(void *arg)
{
  uint32_t gpio_num = (uint32_t) arg;
  // ESP_LOG*は割込み内では？使えない。エラーになる。
  esp_rom_printf("[interrupt!] GPIO=%lu, intr on core=%d, val=%d\n", gpio_num, esp_cpu_get_core_id(), gpio_get_level(gpio_num));
}

void oneshot_interrupt_task(void *pvParameters){
  ESP_LOGI(TAG, "oneshot_interrupt_task start ===>");
  gpio_num_t num = GPIO_NUM_1;
  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = (1ULL << num),
    .pull_down_en = 0,
    .pull_up_en = 0,
  };
  io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
  io_conf.pull_up_en = 1;

  
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_LOGI(TAG, "gpio_config end.");
  ESP_ERROR_CHECK(gpio_set_level(num, 0));
  ESP_LOGI(TAG, "gpio_set_level end.");
  ESP_ERROR_CHECK(gpio_set_intr_type(num, GPIO_INTR_POSEDGE));
  ESP_LOGI(TAG, "gpio_set_intr_type end.");
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_LOGI(TAG, "gpio_install_isr_service end.");
  ESP_ERROR_CHECK(gpio_isr_handler_add(num, gpio_isr_edge_handler, (void *)num));
  ESP_LOGI(TAG, "gpio_isr_handler_add end.");
  ESP_ERROR_CHECK(gpio_set_level(num, 1));
  ESP_LOGI(TAG, "gpio_set_level end.");
  delay_ms(2000);

  ESP_ERROR_CHECK(gpio_isr_handler_remove(num));
  ESP_LOGI(TAG, "gpio_isr_handler_remove end.");
  gpio_uninstall_isr_service();
  ESP_LOGI(TAG, "gpio_uninstall_isr_service end.");
  ESP_LOGI(TAG, "<=== oneshot_interrupt_task end");
  vTaskDelete(NULL);  
}


void oneshot_blink(void *pvParameters){
  ESP_LOGI(TAG, "oneshot_blink start ===>");
  gpio_num_t num = GPIO_NUM_4;
  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = (1ULL << num),
    .pull_down_en = 0,
    .pull_up_en = 0,
  };
  io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
  io_conf.pull_up_en = 1;

  
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_set_level(num, 0));
  ESP_ERROR_CHECK(gpio_set_level(num, 1));
  delay_ms(2000);

  vTaskDelete(NULL);  
}


void app_main()
{
  ESP_LOGI(TAG, "app_main start ===>");
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
  //xTaskCreatePinnedToCore(pro_task, "pro_task", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  //xTaskCreatePinnedToCore(app_task, "app_task", 8192, NULL, 1, NULL, APP_CPU_NUM);
  xTaskCreatePinnedToCore(oneshot_interrupt_task, "oneshot_interrupt_task", 8192, NULL, 1, NULL, APP_CPU_NUM);

  xTaskCreatePinnedToCore(oneshot_blink, "oneshot_blink", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  ESP_LOGI(TAG, "<=== app_main end");
}

