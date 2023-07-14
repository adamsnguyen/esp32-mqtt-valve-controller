#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#define ESP_INTR_FLAG_DEFAULT 0

#define VALVE_1_GPIO_OUT_PIN 16
#define VALVE_2_GPIO_OUT_PIN 17
#define VALVE_3_GPIO_OUT_PIN 18
#define GPIO_OUTPUT_PIN_SEL ((1ULL << VALVE_1_GPIO_OUT_PIN) | (1ULL << VALVE_2_GPIO_OUT_PIN) | (1ULL << VALVE_3_GPIO_OUT_PIN))

#define NUMBER_OF_VALVES 3

typedef struct {
    int valve_id;
    int valve_pin;
} TimerParams_t;

typedef struct {
    int valve_id;
    char name[10];
    bool status;
    int pin;
    int timeout_ms;
    TimerHandle_t timer;
    TimerParams_t xTimerParameters;
} Valve;

Valve valves[] = {  {1, "valve_1", false, VALVE_1_GPIO_OUT_PIN, 1800000, NULL, {1, VALVE_1_GPIO_OUT_PIN}},
                    {2, "valve_2", false, VALVE_2_GPIO_OUT_PIN, 1800000, NULL, {2, VALVE_2_GPIO_OUT_PIN}},
                    {3, "valve_3", false, VALVE_3_GPIO_OUT_PIN, 1800000, NULL, {3, VALVE_3_GPIO_OUT_PIN}}};

char valve1ChangeStatus[] = "/valve1/change_status";
char valve2ChangeStatus[] = "/valve2/change_status";
char valve3ChangeStatus[] = "/valve3/change_status";

char valve1GetStatus[] = "/valve1/get_status";
char valve2GetStatus[] = "/valve2/get_status";
char valve3GetStatus[] = "/valve3/get_status";

char valve1SendStatus[] = "/valve1/send_status";
char valve2SendStatus[] = "/valve2/send_status";
char valve3SendStatus[] = "/valve3/send_status";

static const char *TAG = "MQTT_EXAMPLE";

void on_timer(TimerHandle_t xTimer)
{
    TimerParams_t* pxTimerParameters;

    // pvTimerGetTimerID() returns the ID set by the last call to vTimerSetTimerID()
    // which should be the pointer to TimerParams_t structure
    pxTimerParameters = (TimerParams_t*)pvTimerGetTimerID(xTimer); 

    printf("Valve %d OFF, time hit %lld\n", pxTimerParameters->valve_id, esp_timer_get_time() / 1000);
    gpio_set_level(pxTimerParameters->valve_pin, 0);
    
    valves[pxTimerParameters->valve_id-1].status = false;
}
void start_timer(Valve* valve){
    printf("%s ON\n", valve->name);

    gpio_set_level(valve->pin, 1);
    valve->status = true;

    if (valve->timer == NULL)
    {
        valve->timer = xTimerCreate(valve->name, pdMS_TO_TICKS(valve->timeout_ms), pdFALSE, (void*)&valve->xTimerParameters, on_timer);
        printf("time start %lld\n", esp_timer_get_time() / 1000);
    }
    if (xTimerStart(valve->timer, 0) != pdPASS)
    {
        // The start command could not be sent to the timer service task successfully.
        ESP_LOGE(TAG, "Failed to start timer");
    }
}

void stop_timer(Valve* valve){
    printf("%s ON\n", valve->name);

    gpio_set_level(valve->pin, 0);
    valve->status = false;

    if (valve->timer != NULL)
    {
        // Stops the timer
        xTimerStop(valve->timer, 0);
        // Cleans up the timer resources
        xTimerDelete(valve->timer, 0);

    }
}

int findFirstInt(char *str)
{

    int result = 0;
    for (; *str; str++)
    {
        if (sscanf(str, "%d", &result) == 1)
        {
            break;
        }
    }
    printf("valve: %d\n", result);
    return result;
}

bool containsSubstring(const char *str, const char *substring)
{
    bool result = strstr(str, substring) != NULL;
    return result;
}



static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    // TimerHandle_t *timers = (TimerHandle_t *)handler_args;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve1ChangeStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve2ChangeStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve3ChangeStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve1GetStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve2GetStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, valve3GetStatus, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        // Topics
        char change_status[] = "change_status";
        char get_status[] = "get_status";

        // Messages
        char on[] = "ON";
        char off[] = "OFF";

        int valve = findFirstInt(event->topic);

        bool change_status_topic = false;
        bool change_status_message = false;

        bool get_status_topic = false;

        if (containsSubstring(event->topic, change_status))
        {
            change_status_topic = true;

            if (containsSubstring(event->data, on))
            {
                printf("message is ON\n");
                change_status_message = true;
            }

            if (containsSubstring(event->data, off))
            {
                printf("message is OFF\n");
                change_status_message = false;
            }
        }
        else if (containsSubstring(event->topic, get_status))
        {
            get_status_topic = true;
        }

        if (change_status_topic)
        {
            switch (valve)
            {
            case 1:
                if (change_status_message)
                {
                    start_timer(&valves[0]);                 
                    break;
                }
                else
                {                 
                    gpio_set_level(VALVE_1_GPIO_OUT_PIN, 0);
                    valves[0].status = false;
                    printf("Valve 1 OFF\n");
                }
                break;

            case 2:
                if (change_status_message)
                {
                    start_timer(&valves[1]);
                    break;
                }
                else
                {
                    gpio_set_level(VALVE_2_GPIO_OUT_PIN, 0);
                    valves[1].status = false;
                    printf("Valve 2 OFF\n");
                }
                break;

            case 3:
                if (change_status_message)
                {
                    start_timer(&valves[2]);
                    break;
                }
                else
                {
                    gpio_set_level(VALVE_3_GPIO_OUT_PIN, 0);
                    valves[2].status = false;
                }
                break;

            default:
                break;
            }
        }

        if(get_status_topic && containsSubstring(event->data, "get"))
        {
            get_status_topic = false;
            char result[4];
            char* result_on = "ON";
            char* result_off = "OFF";
            
            if (valves[valve-1].status == true)
            {
                strcpy(result, result_on);
            }
            else
            {
                strcpy(result, result_off);
            }
            
            switch (valve)
            {
            case 1:                
                esp_mqtt_client_publish(client, valve1SendStatus, result, 0, 1, 0);
                break;
            case 2:
                esp_mqtt_client_publish(client, valve2SendStatus, result, 0, 1, 0);
                break;
            case 3:
                esp_mqtt_client_publish(client, valve3SendStatus, result, 0, 1, 0);
                break;
            default:
                break;
            }

        }

        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    mqtt_app_start();
}
