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

#define TWDT_TIMEOUT_MS 2000

#define TAG "mcpwm"

TaskHandle_t taskHandle;
TaskHandle_t taskHandle2;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

//------------------------------
// ADC
//------------------------------
#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_6
#define EXAMPLE_ADC_CHANNEL ADC_CHANNEL_4
void adc1_oneshot_task(void *pvParameters){
  esp_err_t ret = ESP_FAIL;
  //------------------------
  // ADC1 initialize
  //------------------------
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
  };
  ESP_LOGI(TAG, "adc_oneshot_new_unit");
  ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
  ESP_ERROR_CHECK(ret);

  //------------------------
  // ADC1 Configuration
  //------------------------
  adc_oneshot_chan_cfg_t config = {
    .atten = EXAMPLE_ADC_ATTEN,
    .bitwidth = ADC_BITWIDTH_DEFAULT, // default
  };
  ESP_LOGI(TAG, "adc_oneshot_config_channel");
  ret = adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC_CHANNEL, &config);
  ESP_ERROR_CHECK(ret);

  //------------------------
  // ADC1 Calibration Init
  //------------------------
  adc_cali_handle_t adc1_calibration_handle = NULL;
  ESP_LOGI(TAG, "adc_oneshot_config_channel");
  ret = ESP_FAIL;
  bool calibrated = false;
  // esp32c3, esp32s3がadc_cali_create_scheme_curve_fitting()に対応している
  // それ以外はadc_cali_create_scheme_line_fitting()
  adc_cali_curve_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = EXAMPLE_ADC_ATTEN,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_calibration_handle);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Calibration Success");
    calibrated = true;
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(TAG, "Invalid arg or no memory");
  }

  int adc_raw;
  int voltage;
  int voltages[100]={};
  ESP_LOGW(TAG,"start ===>");
  for(int i=0; i<100;i++){
    // adc oneshot
    ret = adc_oneshot_read(adc1_handle, EXAMPLE_ADC_CHANNEL, &adc_raw);
    ESP_ERROR_CHECK(ret);
    //ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC_CHANNEL, adc_raw);
    if (calibrated) {
      ret = adc_cali_raw_to_voltage(adc1_calibration_handle, adc_raw, &voltage);
      ESP_ERROR_CHECK(ret);
      voltages[i] = voltage;
      ESP_LOGI(TAG, "i= %02d voltage= %d [mV]", i, voltage);
      //ESP_LOGI(TAG, "ADC%d Channel[%d] i=%02d, Voltage:%d[mV]", ADC_UNIT_1 + 1, EXAMPLE_ADC_CHANNEL, i, voltage);
    }
    delay_ms(100);
  }
  ESP_LOGW(TAG,"<=== finish");
  uint32_t sum = 0;
  uint32_t sumsq = 0;
  for(int i=0; i<100;i++){
    sum += voltages[i];
    sumsq += (voltages[i]*voltages[i]);
  }
  float mean = sum/100.0;
  float stdnum = sqrt(fabs(sumsq/100.0 - mean * mean));
  ESP_LOGI(TAG, "mean = %.3f, std = %.3f", mean, stdnum);
  while (1) {
    delay_ms(1000);
  }
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
//https://robotools.in/shop/motors-drivers-actuators/n20-micro-gear-motor/n20-motor-with-encoder/n20-6v-150rpm-micro-metal-gear-motor-with-encoder/
//センサーが2個付いていて、1回転でそれぞれのパルスはみんなバラバラの記載調べるしかない

void pwm_task(void *pvParameters){
  while (1) {
    delay_ms(1000);
  }
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

  xTaskCreatePinnedToCore(adc1_oneshot_task, "adc1_oneshot_task", 8192, NULL, 1, &taskHandle, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(pwm_task, "pwm_task", 8192, NULL, 1, &taskHandle2, APP_CPU_NUM);

  ESP_LOGI(TAG, "<=== app_main end");
}
