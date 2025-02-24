## esp32-arduino-micro-ros

platformio向けのmicro-rosライブラリを使用する。
ただし、このライブラリを使う場合、フレームワークはArduinoを指定する必要がある。

ROS2のディストリビューションとmicro-rosエージェントとの通信方式を指定しておく。

* board_microros_distro
* board_microros_transport

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 2000000
monitor_speed = 115200
lib_deps =
    https://github.com/micro-ROS/micro_ros_platformio
board_microros_distro = humble
board_microros_transport = wifi
build_flags =
    -DMICRO_ROS_TRANSPORT_UDP
    -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
    -DARDUINOJSON_USE_LONG_LONG=1
    -DARDUINOJSON_DECODE_UNICODE=1
    -DARDUINOJSON_ENABLE_NAN=1
```

https://github.com/micro-ROS/micro_ros_platformio/tree/main

## micro-rosエージェントのセットアップ

Ubuntu22.04でビルドは確認している。

```
source /opt/ros/humble/setup.bash

mkdir microros_ws
cd microros_ws
git clone -b $ROS_DISTRO https://github.com/micro-ROS/micro_ros_setup.git src/micro_ros_setup

rosdep install --from-paths src --ignore-src -y
colcon build

source install/local_setup.bash
ros2 run micro_ros_setup create_agent_ws.sh
ros2 run micro_ros_setup build_agent.sh
```

エージェントを起動

```
source install/local_setup.sh
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

[1740378724.307047] info     | UDPv4AgentLinux.cpp | init                     | running...             | port: 8888
[1740378724.307336] info     | Root.cpp           | set_verbose_level        | logger setup           | verbose_level: 4
```

## ESP32のビルド

**main.c**と同じ階層に**secret.h**を作成して通信設定などを定義する。

```c
#define WIFI_SSID "MySSID"
#define WIFI_PASSWORD "MyPassword"
#define MICRROS_AGENT_IP "192.168.0.100"
#define MICROROS_AGENT_PORT 8888
```

ビルドしてESP32にアップロードする

## 確認

ESP32を電源ONすると、エージェントとの通信が開始される。

無事ノードやトピックが登録されると、エージェントのコンソールに表示される。


```
[1740381818.567865] info     | Root.cpp           | create_client            | create                 | client_key: 0x60F52EF3, session_id: 0x81
[1740381818.567953] info     | SessionManager.hpp | establish_session        | session established    | client_key: 0x60F52EF3, address: 192.168.2.2:47138
[1740381818.586567] info     | ProxyClient.cpp    | create_participant       | participant created    | client_key: 0x60F52EF3, participant_id: 0x000(1)
[1740381818.592160] info     | ProxyClient.cpp    | create_topic             | topic created          | client_key: 0x60F52EF3, topic_id: 0x000(2), participant_id: 0x000(1)
[1740381818.595781] info     | ProxyClient.cpp    | create_publisher         | publisher created      | client_key: 0x60F52EF3, publisher_id: 0x000(3), participant_id: 0x000(1)
[1740381818.599956] info     | ProxyClient.cpp    | create_datawriter        | datawriter created     | client_key: 0x60F52EF3, datawriter_id: 0x000(5), publisher_id: 0x000(3)
[1740381818.605082] info     | ProxyClient.cpp    | create_topic             | topic created          | client_key: 0x60F52EF3, topic_id: 0x001(2), participant_id: 0x000(1)
[1740381818.608700] info     | ProxyClient.cpp    | create_subscriber        | subscriber created     | client_key: 0x60F52EF3, subscriber_id: 0x000(4), participant_id: 0x000(1)
[1740381818.615315] info     | ProxyClient.cpp    | create_datareader        | datareader created     | client_key: 0x60F52EF3, datareader_id: 0x000(6), subscriber_id: 0x000(4)

```

エージェントが稼働している端末でROS2コマンドで登録状況が確認できる。

ノード一覧

```
source install/local_setup.bash
ros2 node list

/esp32_node
```

トピック一覧

```
ros2 topic list

/esp32_topic
/esp32_topic2
/parameter_events
/rosout
```

トピック詳細

```
ros2 topic info /esp32_topic

Type: std_msgs/msg/Int32
Publisher count: 1
Subscription count: 0
```

ESP32がパブリッシュしている内容をサブスクライブしてみる。

```
ros2 topic echo /esp32_topic
data: 1740379720
---
data: 1740379721
---
data: 1740379722
---
```

ESP32がサブスクライブしているトピックにメッセージを送信してみる。

```
ros2 topic pub /esp32_topic2 std_msgs/msg/Int32 "{data: 42}"
```

ESP32のシリアルモニターでESP32上での送受信内容を確認する。
パブリッシュとサブスクライブの内容が同じタイミングなので交互に表示される。

```
Published UNIX time: 1740382068
Received message: 42
Published UNIX time: 1740382069
Received message: 42
Published UNIX time: 1740382070
Received message: 42
Published UNIX time: 1740382071
Received message: 42
Published UNIX time: 1740382072
Received message: 42
```
