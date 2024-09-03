#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_ERR);

#include <stdio.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/mqtt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#define SLEEP_TIME_MS 100

#define WIFI_SSID "your_wifi_ssid"  // Replace with your Wi-Fi SSID
#define WIFI_PASS "your_wifi_password"  // Replace with your Wi-Fi Password

// MQTT broker details 
#define MQTT_BROKER_ADDR "192.168.1.10" // Replace with your MQTT broker IP
#define MQTT_BROKER_PORT 1883

#define MQTT_CLIENT_ID "zephyr_client"
#define BUTTON_TOPIC "device/button/press"
#define LED_TOPIC "device/led/status"

struct mqtt_client client;
struct sockaddr_in broker;

// The devicetree node identifier for the "led0" alias.
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0 DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS DT_GPIO_FLAGS(LED0_NODE, gpios)
#else
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0 ""
#define PIN 0
#define FLAGS 0
#endif

//  The devicetree node identifier for the "sw1" alias.
#define SW1_NODE DT_ALIAS(sw1)

#if !DT_NODE_HAS_STATUS(SW1_NODE, okay)
#error "Unsupported board: sw1 devicetree alias is not defined"
#endif

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0});
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0});
static struct gpio_callback button_cb_data;
static struct net_mgmt_event_callback dhcp_cb;
static struct zsock_pollfd fds[1];

K_SEM_DEFINE(netif_ready, 0, 1);

// Function protoypes
void mqtt_event_handler(struct mqtt_client *const c, const struct mqtt_evt *evt);
void mqtt_connect_function();
void mqtt_publish_button_event();
void configure_led();
void configure_button();
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void mqtt_subscribe_topics();
void wifi_interface_init_function();

// Button functions
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
    mqtt_publish_button_event();
}

void configure_button()
{
    int ret;
    if (!gpio_is_ready_dt(&button))
    {
        printk("Error: button device %s is not ready\n",
               button.port->name);
        return;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0)
    {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, button.port->name, button.pin);
        return;
    }

    ret = gpio_pin_interrupt_configure_dt(&button,
                                          GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0)
    {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, button.port->name, button.pin);
        return;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    printk("Set up button at %s pin %d\n", button.port->name, button.pin);
}

// LED functions
void configure_led()
{
    int ret;
    if (led.port && !gpio_is_ready_dt(&led))
    {
        printk("Error %d: LED device %s is not ready; ignoring it\n",
               ret, led.port->name);
        led.port = NULL;
    }
    if (led.port)
    {
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
        if (ret != 0)
        {
            printk("Error %d: failed to configure LED device %s pin %d\n",
                   ret, led.port->name, led.pin);
            led.port = NULL;
        }
        else
        {
            printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
        }
    }
}

void turn_on_led()
{
    gpio_pin_set_dt(&led, 1);
}

void turn_off_led()
{
    gpio_pin_set_dt(&led, 0);
}

// MQTT functions

// MQTT Event Handler
void mqtt_event_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
    switch (evt->type)
    {
    case MQTT_EVT_CONNACK:
        LOG_INF("MQTT connected!");
        mqtt_subscribe_topics();
        break;

    case MQTT_EVT_PUBLISH:
    {
        const struct mqtt_publish_param *p = &evt->param.publish;

        if (strncmp(p->message.topic.topic.utf8, LED_TOPIC, p->message.topic.topic.size) == 0)
        {
            if (strncmp(p->message.payload.data, "on", p->message.payload.len) == 0)
            {
                turn_on_led();
            }
            else if (strncmp(p->message.payload.data, "off", p->message.payload.len) == 0)
            {
                turn_off_led();
            }
        }
        break;
    }

    case MQTT_EVT_DISCONNECT:
        LOG_ERR("MQTT disconnected!");
        break;
    
    case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

    case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		int err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

    default:
        break;
    }
}

// Publish Button Event
void mqtt_publish_button_event()
{
    struct mqtt_publish_param param;
    param.message.topic.topic.utf8 = BUTTON_TOPIC;
    param.message.topic.topic.size = strlen(BUTTON_TOPIC);
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.message.payload.data = "Button Pressed";
    param.message.payload.len = strlen("Button Pressed");

    param.retain_flag = 0;

    int rc = mqtt_publish(&client, &param);
    if (rc != 0)
    {
        LOG_ERR("Failed to publish message, error: %d", rc);
    }
    else
    {
        LOG_INF("Button press event published");
    }
}

// Subscribe to MQTT topics
void mqtt_subscribe_topics()
{
    struct mqtt_topic led_topic = {
        .topic = {
            .utf8 = LED_TOPIC,
            .size = strlen(LED_TOPIC),
        },
        .qos = MQTT_QOS_0_AT_MOST_ONCE,
    };

    const struct mqtt_subscription_list subs_list = {
        .list = &led_topic,
        .list_count = 1,
        .message_id = 1234,
    };

    int rc = mqtt_subscribe(&client, &subs_list);
    if (rc != 0)
    {
        LOG_ERR("Failed to subscribe to topics, error: %d", rc);
    }
    else
    {
        LOG_INF("Subscribed to topics");
    }
}

void mqtt_connect_function()
{
    mqtt_client_init(&client);

    client.broker = &broker;
    client.evt_cb = mqtt_event_handler;
    client.client_id.utf8 = MQTT_CLIENT_ID;
    client.client_id.size = strlen(MQTT_CLIENT_ID);
    client.protocol_version = MQTT_VERSION_3_1_1;

    //  Configure broker
    broker.sin_family = AF_INET;
    broker.sin_port = htons(MQTT_BROKER_PORT);
    if (net_addr_pton(AF_INET, MQTT_BROKER_ADDR, &broker.sin_addr) < 0)
    {
        LOG_ERR("Failed to convert broker address");
        return;
    }

    int rc = mqtt_connect(&client);
    if (rc != 0)
    {
        LOG_ERR("MQTT connect failed: %d", rc);
        return;
    }

    LOG_INF("MQTT connection request sent");

    fds[0].fd = client.transport.tcp.sock;
    fds[0].events = ZSOCK_POLLIN;
}



// Wifi functions

void handler_cb(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}
	k_sem_give(&netif_ready);
}

void wifi_interface_init_function(void)
{
	struct net_if *iface;

	net_mgmt_init_event_callback(&dhcp_cb, handler_cb,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&dhcp_cb);

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("wifi interface not available");
		return;
	}

	net_dhcpv4_start(iface);
	k_sem_take(&netif_ready, K_FOREVER);
}


int main(void)
{
    configure_led();
    configure_button();

    wifi_interface_init_function();
    mqtt_connect_function();

    while (1)
    {
        int rc = zsock_poll(fds, 1, 1000);
        if (rc < 0)
        {
            LOG_ERR("Error in poll: %d", rc);
            continue;
        }

        if (fds[0].revents & ZSOCK_POLLIN)
        {
            mqtt_input(&client);
        }

        mqtt_live(&client);
    }
}