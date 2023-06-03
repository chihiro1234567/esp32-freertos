#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include <esp_task_wdt.h>
#include "esp_log.h"

#include "freertos/queue.h"
#include "driver/timer.h"
#include <freertos/task.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "test1"
#define TWDT_TIMEOUT_MS 2000

TaskHandle_t taskHandle;

void delay_ms(uint32_t ms){
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

// DCモーターのエンコーダーのVCC、GNDにESP32の3.3V電源-GNDを接続する
// エンコーダーのC1とC2をそれぞれGPIO5,GPIO6に接続してパルスの立ち上がりをカウント
// JGA12-N20B 150RPMのエンコーダーの磁石を手動で1回転させると、C1、C2それぞれ7カウント(合計14)だった
// 減速比の値はおそらく100:1 https://www.nfpmotor.com/3v-6v-12v-dc-micro-metal-gearmotor-model-nfp-jga12-n20
// 出力軸1回転で100 x 7 = 700カウント

#define REDUCTION_RATIO (100)
#define PULSE_PER_ROTATION (7)
volatile uint16_t edge_counter = 0;
volatile uint8_t direction_forward = 0;

void IRAM_ATTR gpio_isr_edge_handler(void *arg){
  uint32_t gpio_num = (uint32_t) arg;
  // パルスカウント
  edge_counter+=1;
  
  // もう片方のC2パルスのLevel
  // どうもC1の立ち上がりエッジのときにC2は、
  // 正回転：C1=HIGH, C2=HIGH
  // 逆回転：C1=HIGH, C2=LOW
  // らしい
  direction_forward = gpio_get_level(GPIO_NUM_6);

  // 高頻度で割込みを発生させると、コンソール出力でエラーになる
  // 割込み処理内でのコンソール出力が負荷高い（割込み自体優先度が高いのでWDT発動）
  // Guru Meditation Error: Core  0 panic'ed (Interrupt wdt timeout on CPU0)
  // 700カウントで1回転なので700の倍数で出力してみる
  // 出力軸が1回転毎にコンソール出力される

  // 手でエンコーダーを回転させて1周あたりのパルス数をカウントする場合は
  // % 1で毎回出力させる
  if(edge_counter % (PULSE_PER_ROTATION*REDUCTION_RATIO) == 0){ // 7pulse x 100:1
  //if(edge_counter % 1 == 0){
  //if(edge_counter % 210 == 0){ // 7pulse x 30:1
  //if(edge_counter % (12*90) == 0){ // 12 pulse x 90:1
    //esp_rom_printf("gpio=%d, level=%d, edge_counter=%d\n", gpio_num, gpio_get_level(gpio_num), edge_counter);
  }
}
// 割込み設定
void setup_interrupt(){
  ESP_LOGI(TAG, "setup_interrupt start ===>");

  // GPIO=5,6 pulldown, set LOW Level
  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_INPUT,
    .pin_bit_mask = (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_6),
    .pull_down_en = 1,
    .pull_up_en = 0,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_5, 0));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_6, 0));
  ESP_ERROR_CHECK(gpio_set_intr_type(GPIO_NUM_5, GPIO_INTR_POSEDGE));
  ESP_ERROR_CHECK(gpio_set_intr_type(GPIO_NUM_6, GPIO_INTR_POSEDGE));
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_5, gpio_isr_edge_handler, (void *)GPIO_NUM_5));
  //ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_6, gpio_isr_edge_handler, (void *)GPIO_NUM_6));

  ESP_LOGI(TAG, "<=== setup_interrupt end");
}
void calc_velocity_task(void *pvParameters) {
  ESP_LOGW(TAG, "==== calc_velocity_task start ====");

  // 1rpmあたりのrad/s
  float rpm_to_radians = 0.10471975512;
  // 1 rad = 57.29 deg
  float rad_to_deg = 57.29578;
  // モーター回転軸1回転あたりのパルス数 x ギア比 = 出力軸1回転あたりのパルス数
  uint16_t pulse_per_rotation = PULSE_PER_ROTATION*REDUCTION_RATIO;
  
  while (1) {
    uint32_t ulNotifiedValue;
    xTaskNotifyWait(0, 0, &ulNotifiedValue, portMAX_DELAY);
    uint16_t cnt = edge_counter;
    // reset counter
    edge_counter = 0;
    // 1回転あたりのカウント数がpulse_per_rotation、ここから1minsあたりの回転数[rpm]を求める
    float rpm = (float)(cnt * 60 / pulse_per_rotation);
    // rpm_to_radians=0.104に回転数をかけると角速度[rad/s]になる
    float velocity_rad = rpm * rpm_to_radians;
    // rad => degreeで[degree/s]を求める
    float velocity_deg = velocity_rad * rad_to_deg;

    // 5Vでおおよそ
    // 1 [direction] 124 [RPM], 12.99 [rad/s], 744.00 [deg/s], 1448 [encoder]
    ESP_LOGW(TAG, "%d [direction] %.0f [RPM], %.2f [rad/s], %.2f [deg/s], %d [encoder]", direction_forward, rpm, velocity_rad, velocity_deg, cnt);
  }
}
// Timer割込み
void IRAM_ATTR timer_isr_handler(void *arg){
  BaseType_t taskWoken = pdFALSE;
  // 実際の計算や処理はcalc_velocity_taskでやる為通知
  xTaskNotifyFromISR(taskHandle, 0, eIncrement, &taskWoken);
}
// Timerセットアップ
#define TIMER_DIVIDER (80)  // 分周比 80にすると以下の計算が成り立つ
#define TIMER_SCALE   (APB_CLK_FREQ / TIMER_DIVIDER)
// TIMER_SCALE=10000000、1secを1スケールとしている。
// 0.5secなら 0.5 x TIMER_SCALE, 0.01sec => 0.01 x TIMER_SCALEでtimer_set_alarm_value()にセットしている
static void setup_timer(int group, int timer, bool auto_reload, int timer_interval_sec){
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
    .idle_core_mask = 3,
    .trigger_panic = false,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));
  //パルス割込み設定
  setup_interrupt();

  xTaskCreatePinnedToCore(calc_velocity_task, "calc_velocity_task", 8192, NULL, 1, &taskHandle, APP_CPU_NUM);

  // 1secタイマー設定
  setup_timer(TIMER_GROUP_0, TIMER_0, true, 1);

  ESP_LOGI(TAG, "<=== app_main end");
}

