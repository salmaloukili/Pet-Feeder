/* 
 ******************************************************************************
 * @file main.c
 * @brief This file the full implementation of the esp32 webserver and its communication through the nrf24l01 module. 
 * Adapted from the captive portal example provided by Espressif.
 * @author  Salma Loukili
 * @version V1
 * @date   20-March-2023
 ******************************************************************************


Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "esp_http_server.h"
#include "dns_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "mirf.c"
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_sntp.h" 
#include <time.h>
#include <sys/time.h>





#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN
#define MY_VFS_PATH_MAX 1024  
#define CONFIG_RADIO_CHANNEL 97

typedef struct datagram {
    uint8_t dev; // Device ID
    uint8_t command; // Command for the device
    uint8_t data_byte0; // Additional data
    uint8_t data_byte1;
    uint8_t status; // Status byte
} datagram;

NRF24_t dev;

#define LED_ID 1
#define CMD_LIGHT_ON 1
#define CMD_LIGHT_OFF 0
#define CMD_AUTO_ON 2 // sets the light to be automatically turned on when motion is detected
#define CMD_AUTO_OFF 3 // turns off the auto setting

#define SERVO_ID 2
#define CMD_START 1
#define CMD_STOP 2


#define MP3_ID 3
#define CMD_PLAY_AUDIO 1  //data_byte0 for audio number
#define CMD_VOLUME_UP 2
#define CMD_VOLUME_DOWN 3
#define CMD_SPECIFIC_VOL 4  //data_byte0 for volume number
#define CMD_AuTO_AUDIO_ON 5 //data_byte0 for audio number
#define CMD_AUTO_AUDIO_OFF 6

#define LOAD_CELL_ID 4
#define CMD_SEND_WEIGHT 3
#define CMD_THRESHOLD_UP 1
#define CMD_THRESHOLD_DOWN 2

#define MOTION_ID 5
#define CMD_MOTION_DETECTED 1

#define STATUS_OK 0
#define STATUS_ERROR 1

/////////////////////////////////////////////// LOG MESSAGES HANDLING /////////////////////////////////////////////////
#define MAX_MESSAGES 50
typedef struct {
    char message[128];
} Message;

Message messages[MAX_MESSAGES];
int message_count = 0;
int oldest_message_index = 0;

void get_current_time_str(char *buf, size_t buf_len) {
    time_t now;
    struct tm timeinfo;

    time(&now);
    setenv("TZ", "CST-8", 1);
    tzset();

    localtime_r(&now, &timeinfo);
    timeinfo.tm_year += 54;
    timeinfo.tm_mday += 106;
    timeinfo.tm_hour += 5;
    timeinfo.tm_min += 9;

    mktime(&timeinfo);
    strftime(buf, buf_len, "%c", &timeinfo);
}


void add_message(const char *msg) {
    if (!msg) {
        printf("Invalid message: NULL\n");
        return;
    }

    char time_str[64];
    get_current_time_str(time_str, sizeof(time_str));

    char full_message[128];
    snprintf(full_message, sizeof(full_message), "%s: %s", time_str, msg);

    int index = (oldest_message_index + message_count) % MAX_MESSAGES;
    snprintf(messages[index].message, sizeof(messages[index].message), "%s", full_message);

    if (message_count < MAX_MESSAGES) {
        message_count++;
    } else {
        oldest_message_index = (oldest_message_index + 1) % MAX_MESSAGES;
    }


}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////



volatile int weight = 0; 
volatile int volume = 30;
volatile int auto_audio_setting = 0;
volatile int auto_refill_setting = 0;
volatile int default_audio = 1;
volatile int scheduled_dispense_setting = 0;
volatile int scheduled_dispense_time = 0;
volatile int auto_light_setting = 0;


extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

extern const char log_start[] asm("_binary_log_html_start");
extern const char log_end[] asm("_binary_log_html_end");

extern const char settings_start[] asm("_binary_settings_html_start");
extern const char settings_end[] asm("_binary_settings_html_end");


/////////////////////////////////////////////////// SETTING UP NETWORK ////////////////////////////////////////////////
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}


// HTTP GET Handlers
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    ESP_LOGI(TAG, "Serve root");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}
static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

//  'log.html'
static esp_err_t log_html_handler(httpd_req_t *req) {
    httpd_resp_send(req, (const char *)log_start, log_end - log_start);
    return ESP_OK;
}

httpd_uri_t log_uri = {
            .uri       = "/log.html",
            .method    = HTTP_GET,
            .handler   = log_html_handler,
            .user_ctx  = NULL
        };


//  'settings.html'
static esp_err_t settings_html_handler(httpd_req_t *req) {
    httpd_resp_send(req, (const char *)settings_start, settings_end - settings_start);
    return ESP_OK;
}

httpd_uri_t settings_uri = {
            .uri       = "/settings.html",
            .method    = HTTP_GET,
            .handler   = settings_html_handler,
            .user_ctx  = NULL
        };




// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////




//////////////////////////////////////////// DISPENCE FOOD HANDLER ////////////////////////////////////////
static esp_err_t dispense_food_handler(httpd_req_t *req) {
    char buf[100];
    httpd_req_get_url_query_str(req, buf, sizeof(buf));
    char param[32];

    datagram dgram = {
        .dev = SERVO_ID,
        .command = CMD_STOP, // Default to stop
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };


    if (httpd_query_key_value(buf, "command", param, sizeof(param)) == ESP_OK) {
        if (strcmp(param, "start") == 0) {
            dgram.command = CMD_START;
        } else if (strcmp(param, "stop") == 0) {
            dgram.command = CMD_STOP;
        }
    }

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        ESP_LOGI(pcTaskGetName(0), "Send success");
        httpd_resp_send(req, dgram.command == CMD_START ? "Starting dispensing" : "Stopping dispensing", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(pcTaskGetName(0), "Dispense command %d sent", dgram.command);
        char message[128];
        snprintf(message, sizeof(message), "Dispensing food %s", dgram.command == CMD_START ? "started" : "stopped");
        add_message(message);

    } else {
        ESP_LOGW(pcTaskGetName(0), "Send fail");
        httpd_resp_send_500(req);
    }

    return ESP_OK;
}

httpd_uri_t dispense_food_uri = {
    .uri       = "/dispense-food",
    .method    = HTTP_GET,
    .handler   = dispense_food_handler,
    .user_ctx  = NULL
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////// REQUEST FOOD LEVEL ///////////////////////////////////////
static esp_err_t request_food__level_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = LOAD_CELL_ID,
        .command = CMD_SEND_WEIGHT,
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        httpd_resp_send(req, "Request food level command sent", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(pcTaskGetName(0),"Food level request command sent");
    } else {
        ESP_LOGE(TAG, "Send Fail");
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

httpd_uri_t request_food_level_uri = {
    .uri = "/request-weight",
    .method = HTTP_GET,
    .handler = request_food__level_handler,
    .user_ctx = NULL
};



///////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////// PLAY AUDIO HANDLER ////////////////////////////////////////////

static esp_err_t play_audio_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = MP3_ID,
        .command = CMD_PLAY_AUDIO,
        .data_byte0 = 1,  //default to 1
        .data_byte1 = 0,
        .status = STATUS_OK
    };

    char buf[100] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32] = {0};
        if (httpd_query_key_value(buf, "choice", param, sizeof(param)) == ESP_OK) {

            int audio_choice = atoi(param)+1;
            dgram.data_byte0 = audio_choice;
      
            ESP_LOGI(TAG, "Extracted audio choice: %d", audio_choice);
        } else {
            ESP_LOGE(TAG, "Failed to parse audio choice. Defaulting to 1.");
        }
    } else {
        ESP_LOGE(TAG, "Failed to get URL query string.");
    }

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        httpd_resp_send(req, "Audio play command sent", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(pcTaskGetName(0),"Audio play command sent, audio: %d", dgram.data_byte0);
        char message[128];
        snprintf(message, sizeof(message), "Audio %d was played", dgram.data_byte0);
        add_message(message);
    } else {
        ESP_LOGE(TAG, "Send Fail");
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}


httpd_uri_t play_audio_uri = {
    .uri       = "/play-audio",
    .method    = HTTP_GET,
    .handler   = play_audio_handler,
    .user_ctx  = NULL
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////// SET VOLUME /////////////////////////////////////////////

static esp_err_t set_volume_handler(httpd_req_t *req) {
    char content[100];
    int ret, remaining = req->content_len;

    if (remaining <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, content, MIN(remaining, sizeof(content)));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL; 
    }

    cJSON *volume_item = cJSON_GetObjectItem(root, "volume");
    if (!cJSON_IsNumber(volume_item)) {
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int volume_percentage = volume_item->valueint;
    cJSON_Delete(root);

    datagram dgram = {
        .dev = MP3_ID,
        .command = CMD_SPECIFIC_VOL,
        .data_byte0 = MAX(1, MIN(volume_percentage * 30 / 100, 30)),
        .data_byte1 = 0,
        .status = STATUS_OK
    };
   

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        httpd_resp_send(req, "Volume set successfully", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Volume set successfully, volume: %d", dgram.data_byte0);

        char message[128];
        snprintf(message, sizeof(message), "Volume was set to %d", dgram.data_byte0);
        add_message(message);
    } else {
        ESP_LOGE(TAG, "Send Fail");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}


httpd_uri_t set_volume_uri = {
    .uri       = "/set-volume",
    .method    = HTTP_POST,
    .handler   = set_volume_handler,
    .user_ctx  = NULL
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////// THRESHOLD UP DOWN //////////////////////////////////////////

static esp_err_t increase_threshold_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = LOAD_CELL_ID,
        .command = CMD_THRESHOLD_UP,
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        httpd_resp_send(req, "Threshold increased", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Threshold increase command sent");
        char message[128];
        snprintf(message, sizeof(message), "Threshold increased");
        add_message(message);
    } else {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Send Fail");
    }
    return ESP_OK;
}

// Decrease Threshold Handler
static esp_err_t decrease_threshold_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = LOAD_CELL_ID,
        .command = CMD_THRESHOLD_DOWN,
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };

    Nrf24_send(&dev, (uint8_t*)&dgram);
    if (Nrf24_isSend(&dev, 1000)) {
        httpd_resp_send(req, "Threshold decreased", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Threshold decrease command sent");
        char message[128];
        snprintf(message, sizeof(message), "Threshold decreased");
        add_message(message);
    } else {
        httpd_resp_send_500(req);
        ESP_LOGI(TAG, "Send Fail");
    }
    return ESP_OK;
}


httpd_uri_t increase_threshold_uri = {
    .uri       = "/increase-threshold",
    .method    = HTTP_GET,
    .handler   = increase_threshold_handler,
    .user_ctx  = NULL
};

httpd_uri_t decrease_threshold_uri = {
    .uri       = "/decrease-threshold",
    .method    = HTTP_GET,
    .handler   = decrease_threshold_handler,
    .user_ctx  = NULL
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////// TOGGLE LIGHTS HANDLER //////////////////////////////////////////

static esp_err_t light_light_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = LED_ID,
        .command = 0, 
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };


    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "state", param, sizeof(param)) == ESP_OK) {
            dgram.command = (strcmp(param, "on") == 0) ? CMD_LIGHT_ON : CMD_LIGHT_OFF;

		Nrf24_send(&dev, (uint8_t*)&dgram);

		if (Nrf24_isSend(&dev, 1000)) {
			ESP_LOGI(pcTaskGetName(0),"Send success");

			httpd_resp_send(req, "Light command sent successfully", HTTPD_RESP_USE_STRLEN);
            ESP_LOGI(pcTaskGetName(0),"Light command %d sent successfully", dgram.command);
            char message[128];
            snprintf(message, sizeof(message), "Light were toggled %s", dgram.command == CMD_LIGHT_ON ? "on" : "off");
            add_message(message);
        }
			else {
			ESP_LOGW(pcTaskGetName(0),"Send fail:");
			httpd_resp_send_500(req);
		}


        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid or missing 'state' parameter", HTTPD_RESP_USE_STRLEN);
        }
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad request", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}



    httpd_uri_t light_uri = {
        .uri       = "/toggle-light",
        .method    = HTTP_GET,
        .handler   = light_light_handler,
        .user_ctx  = NULL
    };
///////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////// LIGHT & AUDIO SETTINGS //////////////////////////////////////////////////
static esp_err_t lights_setting_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = MP3_ID,
        .command = CMD_AUTO_AUDIO_OFF, 
        .data_byte0 = 0,
        .data_byte1 = 0,
        .status = STATUS_OK
    };

    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "state", param, sizeof(param)) == ESP_OK) {
            dgram.command = (strcmp(param, "on") == 0) ? CMD_AuTO_AUDIO_ON : CMD_AUTO_AUDIO_OFF;

            Nrf24_send(&dev, (uint8_t*)&dgram);

            if (Nrf24_isSend(&dev, 1000)) {
                ESP_LOGI(pcTaskGetName(0), "Send success");
                httpd_resp_send(req, "Light setting command sent successfully", HTTPD_RESP_USE_STRLEN);
                ESP_LOGI(pcTaskGetName(0), "Light setting %d sent successfully", dgram.command);
                char message[128];
                snprintf(message, sizeof(message), "Auto lights setting was toggled %s", dgram.command == CMD_AuTO_AUDIO_ON ? "on" : "off");
                add_message(message);
            } else {
                ESP_LOGW(pcTaskGetName(0), "Send fail:");
                httpd_resp_send_500(req);
            }
        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid or missing 'state' parameter", HTTPD_RESP_USE_STRLEN);
        }
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad request", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

httpd_uri_t light_setting_uri = {
    .uri       = "/toggle-light-setting",
    .method    = HTTP_GET,
    .handler   = lights_setting_handler,
    .user_ctx  = NULL
};

static esp_err_t audio_setting_handler(httpd_req_t *req) {
    datagram dgram = {
        .dev = MP3_ID,
        .command = CMD_AUTO_AUDIO_OFF, 
        .data_byte0 = 1,
        .data_byte1 = 0,
        .status = STATUS_OK,
    };

    char buf[100];
 
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[32];

        if (httpd_query_key_value(buf, "state", param, sizeof(param)) == ESP_OK) {
            dgram.command = (strcmp(param, "on") == 0) ? CMD_AuTO_AUDIO_ON : CMD_AUTO_AUDIO_OFF;

            if (httpd_query_key_value(buf, "audio", param, sizeof(param)) == ESP_OK) {
                dgram.data_byte0 = atoi(param)+1; 
            }
            Nrf24_send(&dev, (uint8_t*)&dgram);
            if (Nrf24_isSend(&dev, 1000)) {
                ESP_LOGI(pcTaskGetName(0), "Send success");
                httpd_resp_send(req, "Audio setting command sent successfully", HTTPD_RESP_USE_STRLEN);
                ESP_LOGI(pcTaskGetName(0), "Auto audio toggled %s, audio choice %d", dgram.command == CMD_AuTO_AUDIO_ON ? "on" : "off", dgram.data_byte0);
                char message[128];
                snprintf(message, sizeof(message), "Auto audio setting was toggled %s, audio choice %d", dgram.command == CMD_AuTO_AUDIO_ON ? "on" : "off", dgram.data_byte0);
                add_message(message);
            } else {
                ESP_LOGW(pcTaskGetName(0), "Send fail");
                httpd_resp_send_500(req);
            }
        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid or missing 'state' parameter", HTTPD_RESP_USE_STRLEN);
        }
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad request", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}


httpd_uri_t audio_setting_uri = {
    .uri       = "/toggle-audio-setting",
    .method    = HTTP_GET,
    .handler   = audio_setting_handler,
    .user_ctx  = NULL
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////// RECEIVE DATAGRAMS ////////////////////////////////////////////////////
void nrf24_receive_task(void *pvParameter) {
	Nrf24_printDetails(&dev);
	ESP_LOGI(pcTaskGetName(0), "Listening...");

	uint8_t buf[5];

	// Clear RX FiFo
	while(1) {
		if (Nrf24_dataReady(&dev) == false) break;
		Nrf24_getData(&dev, buf);
	}

	while(1) {
		if (Nrf24_dataReady(&dev)) {
			Nrf24_getData(&dev, buf);
			ESP_LOGI(pcTaskGetName(0), "Got data:%s", buf);


            datagram received_data = *((datagram*)buf);
	        if (received_data.dev == MOTION_ID && received_data.command == CMD_MOTION_DETECTED) {
                char message[128];
                snprintf(message, sizeof(message), "Motion was detected");
                add_message(message);
                ESP_LOGI("nRF24", "Motion detected. Message logged.");

            } else if (received_data.dev == LOAD_CELL_ID && received_data.command == CMD_SEND_WEIGHT) {
                if (weight != received_data.data_byte0) { // Check if the weight has actually changed
                    weight = received_data.data_byte0; // Update global weight variable
                    char message[128];
                    snprintf(message, sizeof(message), "Updated food level: %d", weight);
                    add_message(message);
                    ESP_LOGI("nRF24", "Updated food level: %d. Message logged.", weight);
                }
            }
		}
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}


}

static esp_err_t get_weight_handler(httpd_req_t *req) {
    char weight_str[32];
    snprintf(weight_str, sizeof(weight_str), "%d", weight); 
    httpd_resp_send(req, weight_str, strlen(weight_str)); 
    return ESP_OK;
}

httpd_uri_t get_weight_uri = {
    .uri       = "/get-weight",
    .method    = HTTP_GET,
    .handler   = get_weight_handler,
    .user_ctx  = NULL
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////


static esp_err_t get_messages_handler(httpd_req_t *req) {
    char* json_response = malloc(2048); 
    if (json_response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    strcpy(json_response, "[");
    int msg_count = 0;

    int start_index = (message_count >= MAX_MESSAGES) ? oldest_message_index : 0;
    int count = MIN(message_count, MAX_MESSAGES);

    for (int i = 0; i < count; i++) {
        int index = (start_index + i) % MAX_MESSAGES;
        if (msg_count > 0) {
            strcat(json_response, ", ");
        }
        strcat(json_response, "\"");
        strcat(json_response, messages[index].message);
        strcat(json_response, "\"");
        msg_count++;
    }

    strcat(json_response, "]");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response); // Send the JSON response
    free(json_response); // Free the allocated memory
    return ESP_OK;
}

httpd_uri_t get_messages_uri = {
    .uri       = "/get-messages",
    .method    = HTTP_GET,
    .handler   = get_messages_handler,
    .user_ctx  = NULL
};

//////////////////////////////////////////// MAIN ////////////////////////////////////////////////////////

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 20;
    config.lru_purge_enable = true;


    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        httpd_register_uri_handler(server, &root);
       
        
        httpd_register_uri_handler(server, &dispense_food_uri);
        httpd_register_uri_handler(server, &play_audio_uri);
       
        httpd_register_uri_handler(server, &get_weight_uri);
        httpd_register_uri_handler(server, &set_volume_uri);
        httpd_register_uri_handler(server, &request_food_level_uri);
       
        httpd_register_uri_handler(server, &increase_threshold_uri);
        httpd_register_uri_handler(server, &decrease_threshold_uri);

        httpd_register_uri_handler(server, &light_uri);

        httpd_register_uri_handler(server, &light_setting_uri);
        httpd_register_uri_handler(server, &audio_setting_uri);

        httpd_register_uri_handler(server, &get_messages_uri);
         httpd_register_uri_handler(server, &log_uri);
        httpd_register_uri_handler(server, &settings_uri);


    }
    return server;
}



void app_main(void)
{

    add_message("System initialized and operational.");

	Nrf24_init(&dev);
	uint8_t payload = sizeof(datagram);
	uint8_t channel = CONFIG_RADIO_CHANNEL;
	Nrf24_config(&dev, channel, payload);


// Slave node 1
	esp_err_t ret = Nrf24_setRADDR(&dev, (uint8_t *)"ABCDE");
	if (ret != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "nrf24l01 not installed");
		while(1) { vTaskDelay(1); }
	}

// Own address	
	ret = Nrf24_setTADDR(&dev, (uint8_t *)"FGHIJ");
	if (ret != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "nrf24l01 not installed");
		while(1) { vTaskDelay(1); }
	}

// Slave node 2
    Nrf24_addRADDR(&dev, 2, (uint8_t *)"KLMNO");

	ESP_LOGW(pcTaskGetName(0), "Set RF Data Ratio to 1MBps");
	Nrf24_SetSpeedDataRates(&dev, 0);
	//////////////////////////////

    

    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop needed by the  main app
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS needed by Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());


    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();

    // Initialise ESP32 in SoftAP mode
    wifi_init_softap();

    // Start the server for the first time
    start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    start_dns_server(&config);

    xTaskCreate(&nrf24_receive_task, "nrf24_receive_task", 4096, &dev, 5, NULL);
    
}
//////////////////////////////////////////////////////////////////////////////////////////////




// todo



// Scheduled dispensing
// setting state saved
// slow page loading

