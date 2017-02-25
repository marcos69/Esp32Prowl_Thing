#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <esp_deep_sleep.h>
#include <driver/rtc_io.h>
#include <driver/adc.h>

#define EspProwl_DEBUG 0

//RTC_DATA_ATTR static int boot_count = 0;
/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID "SSID"
#define EXAMPLE_WIFI_PASS "PASSWORD"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "api.prowlapp.com"
#define WEB_PORT 80
//#define WEB_URL "/publicapi/add"


  // For Prowl, go to
  //   https://www.prowlapp.com/api_settings.php
  // to create an API key.
  // If you don't, the server will return a 401 error code.

// BIG WARNING: DO NOT USE SPACES or %20 in the followin #define's /return 505 error code!!! 

#define APIKEY "****APIKEY*****"
#define APPNAME "ESP32ulpSleeper"
#define EVENT "Tests"
#define PRIORITY 0
#define DESCPRITION "Waked_up_from_deepsleep_Vbat=%fV"

static const char *TAG = "esp32prowl";

static esp_err_t event_handler(void *ctx, system_event_t *event)
    {
        switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
        }
        return ESP_OK;
    }

static void initialise_wifi(void)
    {
        tcpip_adapter_init();
        wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
        ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = EXAMPLE_WIFI_SSID,
                .password = EXAMPLE_WIFI_PASS,
            },
        };
        ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_start() );
    }



void deep_sleep_external_wakeup(uint8_t rtc_gpio_num)
{
  rtc_gpio_init(rtc_gpio_num);
  //gpio_pullup_en(rtc_gpio_num);
  //gpio_pulldown_dis(rtc_gpio_num);
  //esp_deep_sleep_enable_ext0_wakeup(rtc_gpio_num, 0);                         // single pin wake up
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_enable_ext1_wakeup(BIT(rtc_gpio_num), ESP_EXT1_WAKEUP_ALL_LOW); // many pin wake up
  esp_deep_sleep_start();
  //esp_deep_sleep(1000000LL * 20); // time based wakeup
}

#define BLINK_GPIO 5

void stub_task(void *pvParameters)
{
    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(BLINK_GPIO,0);
        vTaskDelay(1000/ portTICK_RATE_MS);
        gpio_set_level(BLINK_GPIO,1);
        vTaskDelay(1000/ portTICK_RATE_MS);
        gpio_set_level(BLINK_GPIO,0);

        deep_sleep_external_wakeup(0);
    }
    vTaskDelete(NULL);
}

float battery()
{
  adc1_config_width(ADC_WIDTH_12Bit);//config adc1 width
  adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_11db);//config channel7 attenuation 
  int val=adc1_get_voltage(ADC1_CHANNEL_7);//get the  val of channel7 =PIN35

  float VBAT = (127.0f/100.0f) * 3.30f * (float) val / 4096.0f;  // LiPo battery
  ESP_LOGI(TAG,"Battery Voltage =%f V",VBAT);
  return VBAT;
}


static void notification_task(void *pvParameters)
    {
      char descritionStr[255];
      sprintf(descritionStr, DESCPRITION, battery());

      const char *REQPART="POST /publicapi/add/?apikey="APIKEY
      "&application="APPNAME"&event="EVENT"&description=%s&priority=%d"
      " HTTP/1.1\r\nHost: api.prowlapp.com\r\nConnection: close\r\n\r\n";

      char preRequest[strlen(REQPART)+255];
      sprintf(preRequest,REQPART,descritionStr,PRIORITY);

        const struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
        };
        struct addrinfo *res;
        struct in_addr *addr;
        int s, r;
        int tryies=0;
        int result=0;
        int leaving=0;
        char recv_buf[255];
        char result_buffer[16];


        while(leaving==0) {
            /* Wait for the callback to set the CONNECTED_BIT in the
               event group.
            */
            xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                                false, true, portMAX_DELAY);
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "Connected to AP");}

            int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

            if(err != 0 || res == NULL) {
                ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            /* Code to print the resolved IP.

               Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
            addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));}

            s = socket(res->ai_family, res->ai_socktype, 0);
            if(s < 0) {
                ESP_LOGE(TAG, "... Failed to allocate socket.");
                freeaddrinfo(res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "... allocated socket\r\n");}

            if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
                close(s);
                freeaddrinfo(res);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "... connected");}
            freeaddrinfo(res);
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "REQUEST=%s LENGTH=%d ",preRequest, strlen(preRequest));}

            if (write(s, preRequest, strlen(preRequest)) < 0) {
                ESP_LOGE(TAG, "... socket send failed");
                close(s);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "... socket send success");}
            /* Read HTTP response */
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "..errno..=%d\r\n",errno);}

              do {
                  bzero(recv_buf, sizeof(recv_buf));
                  r = read(s, recv_buf, sizeof(recv_buf)-1);
                  strncpy(result_buffer, &recv_buf[9], 3);
                  result_buffer[3] = '\0';
                  result = atoi(result_buffer);
                  if (result==200) break;
                  for(int i = 0; i < r; i++) {
                      putchar(recv_buf[i]);
                  }
              } while(r > 0);
              if (result!=200) {
                ESP_LOGE(TAG, "Something got wrong. Last read return=%d errno=%d\r\n", r, errno);
                tryies+=1;
                if (tryies<3) continue;
              }
  if (1) {ESP_LOGI(TAG, "OK, close...");}
            close(s);
            leaving=1;
        }
  if (EspProwl_DEBUG) {ESP_LOGI(TAG, "End Of Post");}
        vTaskDelete(NULL);
        //study_task(NULL);
    }


void app_main()
{
//  ++boot_count;
//  ESP_LOGI(TAG, "Boot count: %d", boot_count);

  nvs_flash_init();
  initialise_wifi();
     printf("Welcome to my Experiment\r\n");
     printf("Try to investigate ULP/RTC...\r\n");
     xTaskCreatePinnedToCore(&notification_task, "notification_task", 4096, NULL, 8, NULL,0);
     vTaskDelay(12000 / portTICK_PERIOD_MS);
     xTaskCreatePinnedToCore(&stub_task,"stub_task",1024,NULL,5,NULL,0);
     while(true) {
         ;
     }

}
