## esp32-freertos-prog16-http-get

* ESP32のWifiでアクセスポイントに接続する
* esp_wifi.h機能を使ってAPIサーバーにHTTP GET通信を定期的に行う

## 設定＆ビルド

main.cと同じ階層に**secret.h**ヘッダファイルを作成して、WIFIやAPIサーバーに接続先URLを定義する。

```h
// src/secret.h
#define WIFI_SSID "MySSID"
#define WIFI_PASSWORD "MyPassword"
#define API_SERVER "http://MyServerIP:8000/"
```

ヘッダファイルを作成＆設定したらコードをビルドする。

## 手順

1. server/app.pyのサーバープログラムを起動しておく
2. ESP32にmain.cを書き込んでHTTP通信を行う
3. シリアルモニタでレスポンスデータの出力を確認する

```
:
I (3572) httpget: =========================
I (3572) httpget: WiFi MAC Address: a0:9e:9e:20:de:32
I (3572) httpget: WiFi IP Address: 192.168.0.101
E (3572) httpget: =========================
I (3582) main_task: Returned from app_main()
I (3602) httpget: http status = 200, content length = 12
I (3822) httpget: read_len=12, received data: "1740307358"
I (4132) httpget: http status = 200, content length = 12
I (4322) httpget: read_len=12, received data: "1740307359"
I (4662) httpget: http status = 200, content length = 12
:
```

## 注意点

esp_http_client_perform()を使ったハンドラ処理ではなく、open => read => closeを行うシンプルな通信処理のサンプル
