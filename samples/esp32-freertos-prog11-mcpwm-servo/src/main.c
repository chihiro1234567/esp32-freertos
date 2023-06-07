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

//------------------------------
// MCPWM
//------------------------------

//ESP32マウスPart.34　MCPWMでモータを回す
//https://rt-net.jp/mobility/archives/10150
//shotaのマイクロマウス研修１６［回路設計④　モータドライバ・エンコーダ回路］
//https://rt-net.jp/mobility/archives/7903
//コラム：パルス幅変調（PWM）とチョークコイル
//https://maxonjapan.com/book/_058/
//ESPIDFのサンプル
//https://github.com/espressif/esp-idf/tree/903af13e847cd301e476d8b16b4ee1c21b30b5c6/examples/peripherals/mcpwm
//ESP32S3 technical manual
//https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf#mcpwm
//ESP32S3 3.10 Peripheral Pin Configurations
//MCPWMはAny GPIOとあるので、どこからでも波形出せる
//https://www.mouser.com/datasheet/2/891/esp32_s3_datasheet_en-2946743.pdf

//エンコーダー付きモーター(6V 150RPM N20)
//esp32-freertos-prog12-encoderでパルス、回転数などカウントするプログラムを作成
//上記の仕様は間違いなさそう
//https://www.nfpmotor.com/3v-6v-12v-dc-micro-metal-gearmotor-model-nfp-jga12-n20

//mcpwm_brushed_dc_control v4.4まで存在
//MCPWMを直接使っている
//https://github.com/espressif/esp-idf/tree/release/v4.4/examples/peripherals/mcpwm/mcpwm_brushed_dc_control

//mcpwm_bdc_speed_control v5以降のサンプル
//mcpwm_brushed_dc_controlのサンプルがv5以降なくなっている
//このサンプルでは公式のアドオン？bdc_motorとpid_ctrlを使っている
//https://github.com/espressif/esp-idf/blob/release/v5.0/examples/peripherals/mcpwm/mcpwm_bdc_speed_control

//Brushed DC Motor Control
//https://github.com/espressif/idf-extra-components/tree/master/bdc_motor
//Proportional integral derivative controller
//https://github.com/espressif/idf-extra-components/tree/master/pid_ctrl
//Rotary Encoder Example
//https://github.com/espressif/esp-idf/blob/release/v5.0/examples/peripherals/pcnt/rotary_encoder/README.md

//2つの特定のPWM信号を生成してブラシ付きDCモーターを駆動する
//最も便利な操作は、前進、後退、コースト、ブレーキである
//DRV8848のようなHブリッジを使用して、ブラシ付きDCモーターに必要な電圧と電流を供給する必要がある
//MCPWMペリフェラル・ドライバのDCモーター制御を簡素化するためのコンポーネントを使っている(bdc_motor)
//この例では、モーターの回転を安定した速度に保つために、簡単なPIDアルゴリズムを使っている(pid_ctrl)
//モータの速度を測定するためにエンコーダーの読み込みを行う為、PCNT周辺機器を使用（上記のRotary Encoder Example参照)

/*
      ESP Board              Servo Motor      5V
+-------------------+     +---------------+    ^
|  SERVO_PULSE_GPIO +-----+PWM        VCC +----+
|                   |     |               |
|               GND +-----+GND            |
+-------------------+     +---------------+
*/

// Raspberry PiでサーボモーターSG90をPWM制御する方法
// https://murasan-net.com/index.php/2022/07/15/raspberry-pi-sg90/
// SG90の場合
// 周期は20ms 50Hz
// -90deg:0.5ms pulse, 0deg:1.45ms pulse, 90deg: 2.4ms pulse

// Please consult the datasheet of your servo before changing the following parameters
#define SERVO_MIN_DEGREE        -90   // Minimum angle
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_DEGREE        90    // Maximum angle
#define SERVO_MAX_PULSEWIDTH_US 2400  // Maximum pulse width in microsecond

#define SERVO_PULSE_GPIO 5        // GPIO connects to the PWM signal line


static inline uint32_t example_angle_to_compare(int angle){
  return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / 
    (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}

void app_main(void){
  int groupid = 0;
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
  ESP_ERROR_CHECK(ret);

  // オペレーター作成とタイマーと接続
  mcpwm_oper_handle_t oper = NULL;
  mcpwm_operator_config_t operator_config = {
    .group_id = groupid, // timerのgroup_idと同じ値
  };
  ret = mcpwm_new_operator(&operator_config, &oper);
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "Connect timer and operator");
  ret = mcpwm_operator_connect_timer(oper, timer);
  ESP_ERROR_CHECK(ret);

  // オペレータを指示して、コンパレーター作成
  ESP_LOGI(TAG, "Create comparator and generator from the operator");
  mcpwm_cmpr_handle_t comparator = NULL;
  mcpwm_comparator_config_t comparator_config = {
    .flags.update_cmp_on_tez = true,
  };
  ret = mcpwm_new_comparator(oper, &comparator_config, &comparator);
  ESP_ERROR_CHECK(ret);

  // オペレータ、出力先GPIOを指示して、ジェネレータ作成
  mcpwm_gen_handle_t generator = NULL;
  mcpwm_generator_config_t generator_config = {
    .gen_gpio_num = SERVO_PULSE_GPIO, // GPIO connects to the PWM signal line
  };
  ret = mcpwm_new_generator(oper, &generator_config, &generator);
  ESP_ERROR_CHECK(ret);
  
  // コンパレータの初期値0deg=>1450を設定
  // set the initial compare value, so that the servo will spin to the center position
  ret = mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(0));
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Set generator action on timer and compare event");
  // go high on counter empty
  mcpwm_gen_timer_event_action_t timer_event = {0};
  timer_event.direction = MCPWM_TIMER_DIRECTION_UP; //UP or DOWN
  timer_event.event = MCPWM_TIMER_EVENT_EMPTY; // EMPTY, FULL, INVALID
  timer_event.action = MCPWM_GEN_ACTION_HIGH; // generator action: KEEP, LOW, HIGH, TOGGLE
  ret = mcpwm_generator_set_action_on_timer_event( generator, timer_event );
  ESP_ERROR_CHECK(ret);

  // go low on compare threshold
  mcpwm_gen_compare_event_action_t compare_event = {0};
  compare_event.direction = MCPWM_TIMER_DIRECTION_UP;
  compare_event.comparator = comparator;
  compare_event.action = MCPWM_GEN_ACTION_LOW;
  ret = mcpwm_generator_set_action_on_compare_event( generator, compare_event );
  ESP_ERROR_CHECK(ret);

  // タイマー開始
  ESP_LOGI(TAG, "Enable and start timer");
  ret = mcpwm_timer_enable(timer);
  ESP_ERROR_CHECK(ret);
  ret = mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);
  ESP_ERROR_CHECK(ret);

  int angle = 0;
  int step = 2;
  while (1) {
    ESP_LOGI(TAG, "Angle of rotation: %d", angle);
    ret = mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle));
    ESP_ERROR_CHECK(ret);
    vTaskDelay(pdMS_TO_TICKS(500));
    // flip angle
    if ((angle + step) > 60 || (angle + step) < -60) {
      step *= -1;
    }
    angle += step;
  }
}
