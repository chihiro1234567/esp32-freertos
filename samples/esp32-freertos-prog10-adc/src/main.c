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

#define TWDT_TIMEOUT_MS 2000

static const char *TAG = "test1";

TaskHandle_t taskHandle;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

// ESP32のADC精度向上
// ADCピン、電源にコンデンサ入れると良いらしい
// https://kohacraft.com/archives/202202081752.html

// ESP32S3のADC
// 2つの12bit SAR ADCユニットを搭載している
// https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
// 温度センサーも内蔵している


// APIとしては、Oneshot、Continuousの2種とCalibration
// ※ver4.*だとadc1_get_raw()というAPIだけだが、ver5.*だと上記3種に変わっている（大半のブログだとver4の内容）

// ESP32 ver4.*対応) ADCのサンプルコードを読み解く
// https://rt-net.jp/mobility/archives/9706
// https://github.com/espressif/esp-idf/blob/release/v3.3/examples/peripherals/adc/main/adc1_example_main.c
// ver5.*以降だと以下のサンプル（Oneshot）
// https://github.com/espressif/esp-idf/blob/903af13e847cd301e476d8b16b4ee1c21b30b5c6/examples/peripherals/adc/oneshot_read/main/oneshot_read_main.c

// https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32s3/api-reference/peripherals/index.html

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// adc_atten_t
// https://docs.espressif.com/projects/esp-idf/en/v4.1.1/api-reference/peripherals/adc.html
// 減衰量の指定、指定する減衰量によって測定できる範囲が異なる
// 測定したい電圧の範囲がレンジ外の場合、分圧などして測定する
// 0 dB attenuation (ADC_ATTEN_DB_0) between 100 and 950 mV
// 2.5 dB attenuation (ADC_ATTEN_DB_2_5) between 100 and 1250 mV
// 6 dB attenuation (ADC_ATTEN_DB_6) between 150 to 1750 mV
// 11 dB attenuation (ADC_ATTEN_DB_11) between 150 to 2450 mV
//#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_11
#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_6

// ADCのチャンネルは20個ほぼどのピンでも読み取れる. ※チャンネルはPin No.ではない
// GPIO PIN
// https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32s3/api-reference/peripherals/gpio.html
// GPIO5 だとADC1-CH4
#define EXAMPLE_ADC_CHANNEL ADC_CHANNEL_4

// 可変抵抗の分圧の値ADCで読み取る
// ESP32のVDD-GND電圧は3.3Vで、ADCのレンジオーバーしてしまうので、
// 1kΩの抵抗と1kΩの可変抵抗を直列して分圧する3.3V/2≒1.65V <= 6dB, 11dB
// https://www.exasub.com/development-kit/esp32-module/how-to-interface-potentiometer-to-esp32-to-read-adc-values/
// VDD-GND, GPIO-GND間にコンデンサを入れると気持ち？安定する（大きな差はない）
// そもそもESP32よりも補正が良く聞いていて、精度が上がっている気がする。
// 10ms間隔で測定しても十分な精度
// 減衰量の測定レンジにもあるが、200mV未満は精度が低い

// ADC_ATTEN_DB_11, 30ms
// real[mV] adc[mV] div[mV] std
// ----------------------------
// 30.8 	15.7 	15.2 	4.6 
// 105.3 	93.0 	12.3 	4.4 
// 202.0 	193.7 	8.3 	5.9 
// 282.0 	276.7 	5.4 	4.5 
// 513.0 	509.0 	4.0 	5.2 
// 736.0 	728.0 	8.0 	4.5 
// 1034.0 	1021.0 	13.0 	4.0 
// 1755.0 	1750.0 	5.0 	4.9 

// ADC_ATTEN_DB_6, 30ms
// real[mV] adc[mV] div[mV] std
// ----------------------------
// 31.0 	24.8 	6.3 	4.8 
// 103.0 	98.4 	4.6 	4.5 
// 203.0 	199.0 	4.0 	2.9 
// 282.0 	279.8 	2.2 	4.9 
// 510.0 	506.0 	4.0 	4.8 
// 737.0 	731.0 	6.0 	4.0 
// 1034.0 	1028.0 	6.0 	3.6 
// 1756.0 	1748.0 	8.0 	4.5 

void adc1_oneshot_task(void *pvParameters){
  esp_err_t ret = ESP_FAIL;
  //------------------------
  // ADC1 initialize
  //------------------------
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1, //adc_unit_t ADC_UNIT_1, ADC_UNIT_2
    //.clk_src //adc_oneshot_clk_src_t
    //.ulp_mode //adc_ulp_mode_t
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
    delay_ms(30);
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

  xTaskCreatePinnedToCore(adc1_oneshot_task, "adc1_oneshot_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);

  ESP_LOGI(TAG, "<=== app_main end");
}
