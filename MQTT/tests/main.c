#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <string.h>

#define LED_PIN 13
#define BUTTON_PIN 0

#define MQTT_BROKER_ADDR "192.168.1.10" // Replace with your broker IP
#define MQTT_BROKER_PORT 1883
#define BUTTON_TOPIC "device/button/press"
#define LED_TOPIC "device/led/status"
#define MQTT_CLIENT_ID "zephyr_test_client"

#define WIFI_SSID "your_wifi_ssid"  // Replace with your Wi-Fi SSID
#define WIFI_PASS "your_wifi_password"  // Replace with your Wi-Fi Password


/* Mock devices for testing */
const struct device *mock_led_dev;
const struct device *mock_button_dev;

/* Mock MQTT client */
static struct mqtt_client client;
static struct sockaddr_in broker;
static struct pollfd fds[1];

/* Setup function to initialize mock devices */
static void setup_mock_devices(void) {
    mock_led_dev = device_get_binding_dt(DT_LABEL(DT_NODELABEL(gpio0)));
    mock_button_dev = device_get_binding(DT_LABEL(DT_NODELABEL(gpio0)));

    zassert_not_null(mock_led_dev, "Failed to bind to LED device");
    zassert_not_null(mock_button_dev, "Failed to bind to Button device");
}

/* Test LED control functions */
static void test_led_on(void) {
    int ret = gpio_pin_configure(mock_led_dev, LED_PIN, GPIO_OUTPUT_ACTIVE);
    zassert_equal(ret, 0, "Failed to configure LED pin");

    gpio_pin_set(mock_led_dev, LED_PIN, 1);
    int value = gpio_pin_get(mock_led_dev, LED_PIN);
    zassert_equal(value, 1, "LED should be ON");
}

static void test_led_off(void) {
    gpio_pin_set(mock_led_dev, LED_PIN, 0);
    int value = gpio_pin_get(mock_led_dev, LED_PIN);
    zassert_equal(value, 0, "LED should be OFF");
}

/* Test Button handling functions */
static void test_button_pressed(void) {
    int ret = gpio_pin_configure(mock_button_dev, BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);
    zassert_equal(ret, 0, "Failed to configure Button pin");

    /* Simulate button press */
    gpio_pin_set(mock_button_dev, BUTTON_PIN, 0);
    int value = gpio_pin_get(mock_button_dev, BUTTON_PIN);
    zassert_equal(value, 0, "Button should be pressed");
}

/* MQTT event handler mock */
static void mock_mqtt_event_handler(struct mqtt_client *const c, const struct mqtt_evt *evt) {
    switch (evt->type) {
        case MQTT_EVT_CONNACK:
            zassert_true(true, "Connected to MQTT broker");
            break;

        case MQTT_EVT_PUBLISH: {
            const struct mqtt_publish_param *p = &evt->param.publish;
            if (strncmp(p->message.topic.topic.utf8, LED_TOPIC, p->message.topic.topic.size) == 0) {
                if (strncmp(p->message.payload.data, "on", p->message.payload.len) == 0) {
                    gpio_pin_set(mock_led_dev, LED_PIN, 1);
                    zassert_true(true, "LED turned on via MQTT");
                } else if (strncmp(p->message.payload.data, "off", p->message.payload.len) == 0) {
                    gpio_pin_set(mock_led_dev, LED_PIN, 0);
                    zassert_true(true, "LED turned off via MQTT");
                }
            }
            break;
        }

        case MQTT_EVT_DISCONNECT:
            zassert_true(false, "MQTT disconnected unexpectedly");
            break;

        default:
            break;
    }
}

/* Test MQTT connection */
static void test_mqtt_connect(void) {
    mqtt_client_init(&client);

    client.broker = &broker;
    client.evt_cb = mock_mqtt_event_handler;
    client.client_id.utf8 = MQTT_CLIENT_ID;
    client.client_id.size = strlen(MQTT_CLIENT_ID);
    client.protocol_version = MQTT_VERSION_3_1_1;

    broker.sin_family = AF_INET;
    broker.sin_port = htons(MQTT_BROKER_PORT);
    int rc = net_addr_pton(AF_INET, MQTT_BROKER_ADDR, &broker.sin_addr);
    zassert_equal(rc, 0, "Failed to set broker address");

    rc = mqtt_connect(&client);
    zassert_equal(rc, 0, "Failed to connect to MQTT broker");

    fds[0].fd = client.transport.tcp.sock;
    fds[0].events = POLLIN;

    mqtt_input(&client);
}

/* Test publishing MQTT button event */
static void test_mqtt_publish_button_event(void) {
    struct mqtt_publish_param param;
    param.message.topic.topic.utf8 = BUTTON_TOPIC;
    param.message.topic.topic.size = strlen(BUTTON_TOPIC);
    param.message.payload.data = "Button Pressed";
    param.message.payload.len = strlen("Button Pressed");
    param.message.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.retain_flag = 0;

    int rc = mqtt_publish(&client, &param);
    zassert_equal(rc, 0, "Failed to publish button press event");
}

/* Main test function */
void test_main(void) {
    setup_mock_devices();

    ztest_test_suite(zephyr_mqtt_tests,
        ztest_unit_test(test_led_on),
        ztest_unit_test(test_led_off),
        ztest_unit_test(test_button_pressed),
        ztest_unit_test(test_mqtt_connect),
        ztest_unit_test(test_mqtt_publish_button_event)
    );

    ztest_run_test_suite(zephyr_iot_tests);
}
