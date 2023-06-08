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

#include "esp_sleep.h"
#include "driver/pulse_cnt.h"

#define TWDT_TIMEOUT_MS 2000

#define TAG "mcpwm"

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

// Pulse Counter
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/pcnt.html
// PCNT (パルスカウンター) モジュールは、入力信号の立ち上がりエッジおよび/または立ち下がりエッジの数をカウントするように設計されています。
// ESP32-S3 には、モジュール内に複数のパルス カウンタ ユニットが含まれています。
// 1各ユニットは事実上、複数のチャネルを持つ独立したカウンタであり、各チャネルは立ち上がり/立ち下がりエッジで
// カウンタをインクリメント/デクリメントできます
// それに加えて、PCNT ユニットには別個のグリッチ フィルターが装備されており、信号からノイズを除去するのに役立ちます。

// Rotary Encoder Example
// https://github.com/espressif/esp-idf/tree/dc016f5987/examples/peripherals/pcnt/rotary_encoder

/*
A      +-----+     +-----+     +-----+
             |     |     |     |
             |     |     |     |
             +-----+     +-----+
B         +-----+     +-----+     +-----+
                |     |     |     |
                |     |     |     |
                +-----+     +-----+

 +--------------------------------------->
                CW direction

      +--------+              +---------------------------------+
      |        |              |                                 |
      |      A +--------------+ GPIO_A (internal pull up)       |
      |        |              |                                 |
+-------+      |              |                                 |
|     | |  GND +--------------+ GND                             |
+-------+      |              |                                 |
      |        |              |                                 |
      |      B +--------------+ GPIO_B (internal pull up)       |
      |        |              |                                 |
      +--------+              +---------------------------------+
*/

#define EXAMPLE_PCNT_HIGH_LIMIT 1000
#define EXAMPLE_PCNT_LOW_LIMIT  -1000

#define EXAMPLE_EC11_GPIO_A 5
#define EXAMPLE_EC11_GPIO_B 6

bool IRAM_ATTR example_pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx){
  BaseType_t high_task_wakeup;
  QueueHandle_t queue = (QueueHandle_t)user_ctx;
  /*
  typedef enum {
    PCNT_UNIT_ZERO_CROSS_POS_ZERO, //start from positive value, end to zero, i.e. +N->0 
    PCNT_UNIT_ZERO_CROSS_NEG_ZERO, //start from negative value, end to zero, i.e. -N->0 
    PCNT_UNIT_ZERO_CROSS_NEG_POS,  //start from negative value, end to positive value, i.e. -N->+M 
    PCNT_UNIT_ZERO_CROSS_POS_NEG,  //start from positive value, end to negative value, i.e. +N->-M 
  } pcnt_unit_zero_cross_mode_t;

  typedef struct {
    int watch_point_value;     //Watch point value that triggered the event
    pcnt_unit_zero_cross_mode_t zero_cross_mode; //Zero cross mode
  } pcnt_watch_event_data_t;
  */
  // send event data to queue, from this interrupt callback
  esp_rom_printf("event = %d, mode = %d\n",edata->watch_point_value, edata->zero_cross_mode);
  xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
  //xQueueSendFromISR(queue, &(edata), &high_task_wakeup);
  return (high_task_wakeup == pdTRUE);
}

void app_main(void){
  ESP_LOGI(TAG, "install pcnt unit");
  pcnt_unit_config_t unit_config = {
      .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
      .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
  };
  pcnt_unit_handle_t pcnt_unit = NULL;
  pcnt_new_unit(&unit_config, &pcnt_unit);

  ESP_LOGI(TAG, "set glitch filter");
  pcnt_glitch_filter_config_t filter_config = {
    .max_glitch_ns = 1000,
  };
  pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

  ESP_LOGI(TAG, "install pcnt channels");
  pcnt_chan_config_t chan_a_config = {
    .edge_gpio_num = EXAMPLE_EC11_GPIO_A,
    .level_gpio_num = EXAMPLE_EC11_GPIO_B,
  };
  pcnt_channel_handle_t pcnt_chan_a = NULL;
  pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a);

  pcnt_chan_config_t chan_b_config = {
    .edge_gpio_num = EXAMPLE_EC11_GPIO_B,
    .level_gpio_num = EXAMPLE_EC11_GPIO_A,
  };

  //PCNTチャネルはエッジタイプとレベルタイプの信号に反応できる
  //単純なアプリケーションの場合は通常、エッジ信号を検出するだけで十分。
  //PCNTチャネルは、つまり、立ち上がりエッジと立ち下がりエッジに反応できる
  //各エッジでユニットのカウンタを増加、減少、または何も行わないように構成できる
  
  //レベル信号はいわゆる制御信号であり、同じチャネルに付加される
  //エッジ信号のカウント モードを制御するために使用されます。
  //エッジ信号とレベル信号の両方の使用を組み合わせることにより
  //PCNT ユニットは直交デコーダとして機能できる。

  //直交デコーダ: デジタル信号のペアで、遷移をカウントする機能
  //直交エンコーダは、物体 (例えば、マウス、トラックボール、ロボットのアクセルなど) の現在の位置、速度、方向を感知できる

  pcnt_channel_handle_t pcnt_chan_b = NULL;
  pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b);
  ESP_LOGI(TAG, "set edge and level actions for pcnt channels");
  pcnt_channel_set_edge_action(
    pcnt_chan_a,
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,
    PCNT_CHANNEL_EDGE_ACTION_INCREASE
  );
  pcnt_channel_set_level_action(
    pcnt_chan_a,
    PCNT_CHANNEL_LEVEL_ACTION_KEEP,

    PCNT_CHANNEL_LEVEL_ACTION_INVERSE
  );
  pcnt_channel_set_edge_action(
    pcnt_chan_b,
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    PCNT_CHANNEL_EDGE_ACTION_DECREASE
  );
  pcnt_channel_set_level_action(
    pcnt_chan_b,
    PCNT_CHANNEL_LEVEL_ACTION_KEEP,
    PCNT_CHANNEL_LEVEL_ACTION_INVERSE
  );

  // Watch Point? トリガーとなる閾値？
  // ここで設定した最大値、最小値を超えると自動的にゼロクリアされる
  ESP_LOGI(TAG, "add watch points and register callbacks");
  int watch_points[] = {
    EXAMPLE_PCNT_LOW_LIMIT,
    EXAMPLE_PCNT_LOW_LIMIT/2,
    0,
    EXAMPLE_PCNT_HIGH_LIMIT/2,
    EXAMPLE_PCNT_HIGH_LIMIT
  };
  for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++) {
    pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]);
  }
  // コールバック登録
  pcnt_event_callbacks_t cbs = {
    .on_reach = example_pcnt_on_reach,
  };
  QueueHandle_t queue = xQueueCreate(10, sizeof(int));
  pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue);

  ESP_LOGI(TAG, "enable pcnt unit");
  pcnt_unit_enable(pcnt_unit);
  ESP_LOGI(TAG, "clear pcnt unit");
  pcnt_unit_clear_count(pcnt_unit);
  ESP_LOGI(TAG, "start pcnt unit");
  pcnt_unit_start(pcnt_unit);

  // Report counter value
  int pulse_count = 0;
  int event_count = 0;
  pcnt_watch_event_data_t edata = {0};
  while (1) {
    event_count = -1;
    if (xQueueReceive(queue, &edata, pdMS_TO_TICKS(0))) {
      // 割込み関数から送信された値
      pcnt_unit_get_count(pcnt_unit, &pulse_count);
      ESP_LOGI(TAG, "[pulse count] %d, [event count] %d", pulse_count, event_count);
    }else{
      pcnt_unit_get_count(pcnt_unit, &pulse_count);
      ESP_LOGI(TAG, "[pulse count] %d", pulse_count);
    }
    delay_ms(1);
  }
}