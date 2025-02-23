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
  int gpio_num1 = 5;

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
  // オペレーター作成とタイマーと接続
  mcpwm_oper_handle_t oper = NULL;
  mcpwm_operator_config_t operator_config = {
    .group_id = groupid, // timerのgroup_idと同じ値
  };
  ret = mcpwm_new_operator(&operator_config, &oper);
  ret = mcpwm_operator_connect_timer(oper, timer);
  // コンパレータ
  mcpwm_cmpr_handle_t comparator = NULL;
  mcpwm_comparator_config_t comparator_config = {
    .flags.update_cmp_on_tez = true,
  };
  ret = mcpwm_new_comparator(oper, &comparator_config, &comparator);
  // オペレータ、出力先GPIOを指示して、ジェネレータ作成
  mcpwm_gen_handle_t generator = NULL;
  mcpwm_generator_config_t generator_config = {
    .gen_gpio_num = gpio_num1,
  };
  ret = mcpwm_new_generator(oper, &generator_config, &generator);
  
  // コンパレータの初期値
  ret = mcpwm_comparator_set_compare_value(comparator, 0);

  mcpwm_gen_timer_event_action_t timer_event = {0};
  timer_event.direction = MCPWM_TIMER_DIRECTION_UP; //UP or DOWN
  timer_event.event = MCPWM_TIMER_EVENT_EMPTY; // EMPTY, FULL, INVALID
  timer_event.action = MCPWM_GEN_ACTION_HIGH; // generator action: KEEP, LOW, HIGH, TOGGLE
  ret = mcpwm_generator_set_action_on_timer_event( generator, timer_event );

  mcpwm_gen_compare_event_action_t compare_event = {0};
  compare_event.direction = MCPWM_TIMER_DIRECTION_UP;
  compare_event.comparator = comparator;
  compare_event.action = MCPWM_GEN_ACTION_LOW;
  ret = mcpwm_generator_set_action_on_compare_event( generator, compare_event );

  // enable & start timer
  ret = mcpwm_timer_enable(timer);
  ret = mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);

  while (1) {
    // コンパレータの入力範囲は0～period_ticksまで
    // period_ticks/2だとduty比=0.5の矩形波になる
    ret = mcpwm_comparator_set_compare_value(comparator, 0);
    delay_ms(2000);
    ret = mcpwm_comparator_set_compare_value(comparator, 5000);
    delay_ms(2000);
    ret = mcpwm_comparator_set_compare_value(comparator, 10000);
    delay_ms(2000);
    ret = mcpwm_comparator_set_compare_value(comparator, 15000);
    delay_ms(2000);
    ret = mcpwm_comparator_set_compare_value(comparator, 20000);
    delay_ms(2000);
  }
}
