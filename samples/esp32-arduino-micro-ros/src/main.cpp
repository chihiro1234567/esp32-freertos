#include <Arduino.h>
#include <WiFi.h>
#include <micro_ros_platformio.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/int32.h>
#include <time.h>

#include "secret.h"

rcl_publisher_t publisher;
std_msgs__msg__Int32 pub_msg;

rcl_subscription_t subscriber;
std_msgs__msg__Int32 sub_msg;

rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_timer_t timer;

// タイマーコールバック関数（1秒ごとに実行）
void timer_callback(rcl_timer_t *timer, int64_t last_call_time) {
  time_t now = time(NULL);
  pub_msg.data = static_cast<int32_t>(now);

  // メッセージをパブリッシュ
  rcl_ret_t ret = rcl_publish(&publisher, &pub_msg, NULL);
  if (ret == RCL_RET_OK) {
    Serial.printf("Published UNIX time: %d\n", pub_msg.data);
  } else {
    Serial.printf("rcl_publish failed: %d\n", ret);
  }
}

// サブスクライバーのコールバック関数
void subscription_callback(const void *msgin) {
  const std_msgs__msg__Int32 *msg = (const std_msgs__msg__Int32 *)msgin;
  Serial.printf("Received message: %d\n", msg->data);
}

void setup() {
  Serial.begin(115200);

  // Wifiの設定を行う
  IPAddress agent_ip;
  agent_ip.fromString(MICRROS_AGENT_IP);
  uint16_t agent_port = MICROROS_AGENT_PORT;
  set_microros_wifi_transports(WIFI_SSID, WIFI_PASSWORD, agent_ip, agent_port);
  delay(2000);

  // NTPサーバーに接続して時間を調整する
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(3000);

  allocator = rcl_get_default_allocator();
	rclc_support_init(&support, 0, NULL, &allocator);
  rcl_node_t node;
  rclc_node_init_default(&node, "esp32_node", "", &support);

  //publisherの作成
  rclc_publisher_init_best_effort(
    &publisher,
    &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
    "/esp32_topic");
  
  // Subscriber の作成
  rclc_subscription_init_best_effort(
    &subscriber,
    &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
    "/esp32_topic2"
  );

  // タイマーの作成（1秒ごとに timer_callback 実行）
  rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(1000), timer_callback);

  // Executor の作成
  int callback_size = 2;
  executor = rclc_executor_get_zero_initialized_executor();
  rclc_executor_init(&executor, &support.context, callback_size, &allocator);
  rclc_executor_add_subscription(&executor, &subscriber, &sub_msg, &subscription_callback, ON_NEW_DATA);
  rclc_executor_add_timer(&executor, &timer);
}

void loop() {
  // Executor をスピン（サブスクライバーとタイマーの処理を実行）
  rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
}
