
#include <stdio.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define SLEEP_TIME_MS 100

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

// The devicetree node identifier for the "sw1" alias.
#define SW1_NODE DT_ALIAS(sw1)

#if !DT_NODE_HAS_STATUS(SW1_NODE, okay)
#error "Unsupported board: sw1 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0});
static uint8_t led_state = false;

// Service and Characteristics UUIDs
static struct bt_uuid_128 led_state_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x9c85a726, 0xb7f1, 0x11ec, 0xb909, 0x0242ac120002));

#define LED_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0xf7547938, 0x68ba, 0x11ec, 0x90d6, 0x0242ac120003)

static struct bt_uuid_128 led_svc_uuid = BT_UUID_INIT_128(LED_SERVICE_UUID_VAL);

// Function prototypes
void configure_led();
void configure_button();
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

// Advertisement Data
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, LED_SERVICE_UUID_VAL),
};

// GAP callbacks
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		printk("Connection failed (err 0x%02x)\n", err);
	}
	else
	{
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

// GATT Access Callbacks

static ssize_t read_led_state(struct bt_conn *conn,
							  const struct bt_gatt_attr *attr, void *buf,
							  uint16_t len, uint16_t offset)
{
	const uint8_t *value = attr->user_data;
	printk("Value 0x%x read.\n", *value);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

static ssize_t write_led_state(struct bt_conn *conn,
							   const struct bt_gatt_attr *attr, const void *buf,
							   uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;
	*value = *((uint8_t *)buf);
	printk("Value 0x%x written.\n", *value);

	printk("Current LED state %s - turning LED %s\n", led_state ? "off" : "on",
		   led_state ? "on" : "off");
	gpio_pin_set_dt(&led, led_state);
	return len;
}

// Attribute Table

BT_GATT_SERVICE_DEFINE(
	led_svc, BT_GATT_PRIMARY_SERVICE(&led_svc_uuid),
	BT_GATT_CHARACTERISTIC(&led_state_char_uuid.uuid,
						   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
						   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
						   read_led_state, write_led_state, &led_state), );

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
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

int main(void)
{
	int err;
	configure_led();
	configure_button();

	err = bt_enable(NULL);
	if (err)
	{
		printk("Bluetooth init failed (err %d)\n", err);
		return 1;
	}
	printk("Bluetooth initialized\n");

	// start avertising
	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err)
	{
		printk("Advertising failed to start (err %d)\n", err);
		return 1;
	}
	printk("Advertising successfully started\n");

	printk("Press the button\n");
	if (led.port)
	{
		while (1)
		{
			/* If we have an LED, match its state to the button's. */
			int val = gpio_pin_get_dt(&button);

			if (val >= 0)
			{
				gpio_pin_set_dt(&led, val);
			}
			k_msleep(SLEEP_TIME_MS);
		}
	}
	return 0;
}
