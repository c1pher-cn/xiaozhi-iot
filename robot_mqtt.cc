/*
    ESP-SparkBot 的底座
    https://gitee.com/esp-friends/esp_sparkbot/tree/master/example/tank/c2_tracked_chassis
*/

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>

#include <cstring>


#include "iot/thing.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "esp_event.h"       // 事件处理机制
#include "esp_wifi.h"        // Wi-Fi 驱动
#include "esp_netif.h"       // 网络接口

#define TAG "robot_mqtt"
#define MQTT_URI "mqtt://192.168.1.1:1883"
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group = NULL;


namespace iot {

class RobotMqtt : public Thing {
 private:
  esp_mqtt_client_handle_t client;
  bool mqtt_connected = false;

  // MQTT 发送消息函数
  void SendMqttMessage(const std::string& message) {
    if (client == nullptr) {
      ESP_LOGE(TAG, "MQTT client is not initialized!");
      return;
    }
    if (!mqtt_connected) {
      ESP_LOGE(TAG, "MQTT is not connected! Cannot send message.");
      return;
  }

    const char* topic = "tankrobot-topic";  // 替换成你需要发布消息的主题
    int msg_id = esp_mqtt_client_publish(client, topic, message.c_str(), 0, 1,
                                         0);  // 发布消息
    ESP_LOGI(TAG, "Message sent, msg_id:%d,topic:%s,msg:%s", msg_id,topic,message.c_str());
  }
  static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                      int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
      ESP_LOGI(TAG, "Wi-Fi disconnected, reconnecting...");
      esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
      //ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa(&event->ip_info.ip));
      xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }
  // MQTT 事件处理函数
  //static void mqtt_event_handler(void* handler_args, esp_event_base_t base,int32_t event_id, void* event_data) {
  static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
      // 获取当前对象指针
    RobotMqtt* self = static_cast<RobotMqtt*>(handler_args);
    ESP_LOGI(TAG, "MQTT connected successfully");
    switch (event_id) {
      case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "mqtt_event_handler");
        self->mqtt_connected = true;
        break;
      case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        self->mqtt_connected = false;  
        break;
      case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribed to topic");
        break;
      case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "Unsubscribed from topic");
        break;
      case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Message published");
        break;
      case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT event error");
        break;
      default:

        break;
    }
  }
  static void mqtt_task(void* pvParameters) {
    RobotMqtt* self = static_cast<RobotMqtt*>(pvParameters);  // 获取当前对象指针
    self->RunMqttTask();  // 调用非静态成员函数
}
  void RunMqttTask() {
    ESP_LOGE(TAG, "wait wifi connection ...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGE(TAG, "wifi connected ...");
    mqtt_app_start();
    ESP_LOGE(TAG, "mqtt connected ...");
    vTaskDelete(NULL);
  }

  void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {.address = {
                              .uri = MQTT_URI
                    }},
        // 你可以在此处扩展其他 MQTT 配置项
        .credentials = {
            .username = "ABCD",  // 替换为你的 MQTT 用户名
            .authentication = {.password = "ABCD" }  // 替换为你的 MQTT 密码
        }
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == nullptr) {
      ESP_LOGE(TAG, "Failed to initialize MQTT client");
      return;
    }

    //esp_mqtt_client_register_event(
    //    client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
    //    mqtt_event_handler, NULL);
    // 注册事件处理函数，并传递当前对象指针
    esp_mqtt_client_register_event(
          client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
          mqtt_event_handler, this);
    ESP_LOGE(TAG, "initialize MQTT client");
    esp_mqtt_client_start(client);
  }

  public:
    RobotMqtt()
      : Thing("RobotMqtt", "小机器人的底座：有履带可以移动；可以用机械抓抓取东西") ,mqtt_connected(false){
    wifi_event_group = xEventGroupCreate();   
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,&wifi_event_handler, NULL)); 
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,&wifi_event_handler, NULL)); 
    
    xTaskCreate(mqtt_task, "mqtt_task", 4096, this, 5, NULL);

    // 定义设备可以被远程执行的指令
    methods_.AddMethod("GoForward", "向前走", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("前进");
                       });

    methods_.AddMethod("GoBack", "向后退", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("后退");
                       });

    methods_.AddMethod("TurnLeft", "向左转", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("左转");
                       });

    methods_.AddMethod("TurnRight", "向右转", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("右转");
                       });

    methods_.AddMethod("Dance", "跳舞", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("跳舞");
                       });
    methods_.AddMethod("Catch", "用机械抓抓东西", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("抓起");
                       });
    methods_.AddMethod("Release", "用机械放下东西", ParameterList(),
                       [this](const ParameterList& parameters) {
                         SendMqttMessage("释放");
                       });
  }
};

}  // namespace iot

DECLARE_THING(RobotMqtt);
