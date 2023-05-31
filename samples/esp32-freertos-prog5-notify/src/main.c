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

// https://qiita.com/azuki_bar/items/7f3aecc8bb1928f6a823

// 送信側
// xTaskNotify(xTaskNotifyFromISR) => xTaskNotifyGive(xTaskNotifyGiveFromISR)
// ※xTaskNotifyGiveはシンプルだが柔軟性DOWN、それぞれ割込み関数内で使うFromISRバージョンがある
// xTaskNotifyWait => ulTaskNotifyTake
// ※ulTaskNotifyTakeはシンプルだが柔軟性DOWN


void pro_task(void *pvParameters) {
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  while (1) {
    //ESP_LOGI(TAG, "pro_task");
    ESP_ERROR_CHECK(esp_task_wdt_reset());
    delay_ms(5);
  }
}
void app_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== app_task start ====");
  while (1) {
    ESP_LOGW(TAG, "notify wait....");
    uint32_t ulNotifiedValue;
    xTaskNotifyWait(0, 0, &ulNotifiedValue, portMAX_DELAY);
    ESP_LOGW(TAG, "notify received.");
    delay_ms(1000);
  }
}

TaskHandle_t taskHandle;

// GPIO割込み
void IRAM_ATTR gpio_isr_handler(void *arg){
  uint32_t gpio_num = (uint32_t) arg;
  // ESP_LOG*は割込み内では？使えない。エラーになる。
  esp_rom_printf("[interrupt!] GPIO=%lu, intr on core=%d, val=%d\n", gpio_num, esp_cpu_get_core_id(), gpio_get_level(gpio_num));

  // Notify送信(ISR版)
  // https://qiita.com/azuki_bar/items/7f3aecc8bb1928f6a823#xtasknotify-xtasknotifyfromisr-api-functions
  BaseType_t taskWoken = pdFALSE;
  
  xTaskNotifyFromISR(taskHandle, 0, eIncrement, &taskWoken);
  //xTaskNotifyFromISR(taskHandle, 0, eSetValueWithoutOverwrite, &taskWoken);
  esp_rom_printf("[interrupt!] xTaskNotifyFromISR\n");
  //portYIELD_FROM_ISR( taskWoken );
  //esp_rom_printf("[interrupt!] portYIELD_FROM_ISR\n");
}

// GPIO：割込み設定、プログラムでLOW=>HIGH
void gpio_trriger(){

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

  gpio_config(&io_conf);
  ESP_LOGI(TAG, "gpio_config end.");
  gpio_set_level(num, 0);
  ESP_LOGI(TAG, "gpio_set_level end.");

  gpio_set_intr_type(num, GPIO_INTR_POSEDGE);
  ESP_LOGI(TAG, "gpio_set_intr_type end.");
  gpio_install_isr_service(0);
  ESP_LOGI(TAG, "gpio_install_isr_service end.");
  gpio_isr_handler_add(num, gpio_isr_handler, (void *)num);
  ESP_LOGI(TAG, "gpio_isr_handler_add end.");

  delay_ms(2000);

  // 連続で発火させてみる
  // notify => waitは必ずしも1対1ではなく、
  // notifyが短時間で連続すると、waitが1回しかコールされないことがある
  // 通知の欠点
  // https://lang-ship.com/blog/work/esp32-freertos-l04-interrupt/
  for(int i=0; i<3;i++){
    // GPIO=ONでトリガー
    gpio_set_level(num, 1);
    gpio_set_level(num, 0);
    ESP_LOGI(TAG, "gpio_set_level end.");
    //delay_ms(100);
  }

  gpio_isr_handler_remove(num);
  ESP_LOGI(TAG, "gpio_isr_handler_remove end.");
  gpio_uninstall_isr_service();
  ESP_LOGI(TAG, "gpio_uninstall_isr_service end.");
  
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
  //xTaskCreatePinnedToCore(pro_task, "pro_task", 8192, NULL, 1, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(app_task, "app_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);

  // 1回だけGPIO発火させる
  gpio_trriger();
  ESP_LOGI(TAG, "<=== app_main end");
}

