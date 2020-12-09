/*
 * Copyright (c) 2018 Phytec Messtechnik GmbH
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <display/cfb.h>
#include <sys/printk.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <drivers/sensor.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh/access.h>

#include "mesh.h"
#include "board.h"
#include "distance_estimator.h"

enum font_size {
	FONT_SMALL = 0,
	FONT_MEDIUM = 1,
	FONT_BIG = 2,
};

enum screen_ids {
	SCREEN_MAIN = 0,
	MY_SCREEN = 1,
	SCREEN_SENSORS = 2,
	SCREEN_STATS = 3,
	SCREEN_LAST,
};

struct font_info {
	uint8_t columns;
} fonts[] = {
	[FONT_BIG] =    { .columns = 12 },
	[FONT_MEDIUM] = { .columns = 16 },
	[FONT_SMALL] =  { .columns = 25 },
};

#define LONG_PRESS_TIMEOUT K_SECONDS(0.5)
#define SENSOR_VALUES_REFRESH_INTERVAL K_SECONDS(1)

#define STAT_COUNT 128

static const struct device *display_dev;
static bool pressed;
static uint8_t screen_id = SCREEN_MAIN;
static const struct device *gpio;
static struct k_delayed_work display_work;
static struct k_delayed_work long_press_work;
static struct k_delayed_work sensor_values_work;
static char str_buf[256];
static int proximity;
static int temperature;

static struct {
	const struct device *dev;
	const char *name;
	gpio_pin_t pin;
	gpio_flags_t flags;
} leds[] = {
	{ .name = DT_GPIO_LABEL(DT_ALIAS(led0), gpios),
	  .pin = DT_GPIO_PIN(DT_ALIAS(led0), gpios),
	  .flags = DT_GPIO_FLAGS(DT_ALIAS(led0), gpios)},
	{ .name = DT_GPIO_LABEL(DT_ALIAS(led1), gpios),
	  .pin = DT_GPIO_PIN(DT_ALIAS(led1), gpios),
	  .flags = DT_GPIO_FLAGS(DT_ALIAS(led1), gpios)},
	{ .name = DT_GPIO_LABEL(DT_ALIAS(led2), gpios),
	  .pin = DT_GPIO_PIN(DT_ALIAS(led2), gpios),
	  .flags = DT_GPIO_FLAGS(DT_ALIAS(led2), gpios)}
};

struct k_delayed_work led_timer;

static size_t print_line(enum font_size font_size, int row, const char *text,
			 size_t len, bool center)
{
	uint8_t font_height, font_width;
	uint8_t line[fonts[FONT_SMALL].columns + 1];
	int pad;

	cfb_framebuffer_set_font(display_dev, font_size);

	len = MIN(len, fonts[font_size].columns);
	memcpy(line, text, len);
	line[len] = '\0';

	if (center) {
		pad = (fonts[font_size].columns - len) / 2U;
	} else {
		pad = 0;
	}

	cfb_get_font_size(display_dev, font_size, &font_width, &font_height);

	if (cfb_print(display_dev, line, font_width * pad, font_height * row)) {
		printk("Failed to print a string\n");
	}

	return len;
}

static size_t get_len(enum font_size font, const char *text)
{
	const char *space = NULL;
	size_t i;

	for (i = 0; i <= fonts[font].columns; i++) {
		switch (text[i]) {
		case '\n':
		case '\0':
			return i;
		case ' ':
			space = &text[i];
			break;
		default:
			continue;
		}
	}

	/* If we got more characters than fits a line, and a space was
	 * encountered, fall back to the last space.
	 */
	if (space) {
		return space - text;
	}

	return fonts[font].columns;
}

void board_blink_leds(void)
{
	k_delayed_work_submit(&led_timer, K_MSEC(100));
}

void board_show_text(const char *text, bool center, k_timeout_t duration)
{
	int i;

	cfb_framebuffer_clear(display_dev, false);

	for (i = 0; i < 3; i++) {
		size_t len;

		while (*text == ' ' || *text == '\n') {
			text++;
		}

		len = get_len(FONT_BIG, text);
		if (!len) {
			break;
		}

		text += print_line(FONT_BIG, i, text, len, center);
		if (!*text) {
			break;
		}
	}

	cfb_framebuffer_finalize(display_dev);

	if (!K_TIMEOUT_EQ(duration, K_FOREVER)) {
		k_delayed_work_submit(&display_work, duration);
	}
}

static struct stat {
	uint16_t addr;
	char name[9];
	uint8_t min_hops;
	uint8_t max_hops;
	uint16_t hello_count;
	uint16_t heartbeat_count;
} stats[STAT_COUNT] = {
	[0 ... (STAT_COUNT - 1)] = {
		.min_hops = BT_MESH_TTL_MAX,
		.max_hops = 0,
	},
};

static uint32_t stat_count;

#define NO_UPDATE -1

static int add_hello(uint16_t addr, const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(stats); i++) {
		struct stat *stat = &stats[i];

		if (!stat->addr) {
			stat->addr = addr;
			strncpy(stat->name, name, sizeof(stat->name) - 1);
			stat->hello_count = 1U;
			stat_count++;
			return i;
		}

		if (stat->addr == addr) {
			/* Update name, incase it has changed */
			strncpy(stat->name, name, sizeof(stat->name) - 1);

			if (stat->hello_count < 0xffff) {
				stat->hello_count++;
				return i;
			}

			return NO_UPDATE;
		}
	}

	return NO_UPDATE;
}

static int add_heartbeat(uint16_t addr, uint8_t hops)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(stats); i++) {
		struct stat *stat = &stats[i];

		if (!stat->addr) {
			stat->addr = addr;
			stat->heartbeat_count = 1U;
			stat->min_hops = hops;
			stat->max_hops = hops;
			stat_count++;
			return i;
		}

		if (stat->addr == addr) {
			if (hops < stat->min_hops) {
				stat->min_hops = hops;
			} else if (hops > stat->max_hops) {
				stat->max_hops = hops;
			}

			if (stat->heartbeat_count < 0xffff) {
				stat->heartbeat_count++;
				return i;
			}

			return NO_UPDATE;
		}
	}

	return NO_UPDATE;
}

void board_add_hello(uint16_t addr, const char *name)
{
	uint32_t sort_i;

	sort_i = add_hello(addr, name);
	if (sort_i != NO_UPDATE) {
	}
}

void board_add_heartbeat(uint16_t addr, uint8_t hops)
{
	uint32_t sort_i;

	sort_i = add_heartbeat(addr, hops);
	if (sort_i != NO_UPDATE) {
	}
}

static void show_statistics(void)
{
	int top[4] = { -1, -1, -1, -1 };
	int len, i, line = 0;
	struct stat *stat;
	char str[32];

	cfb_framebuffer_clear(display_dev, false);

	len = snprintk(str, sizeof(str),
		       "Own Address: 0x%04x", mesh_get_addr());
	
	print_line(FONT_SMALL, line++, str, len, false);

	len = snprintk(str, sizeof(str),
		       "Node Count:  %u", stat_count + 1);
	print_line(FONT_SMALL, line++, str, len, false);

	/* Find the top sender */
	for (i = 0; i < ARRAY_SIZE(stats); i++) {
		int j;

		stat = &stats[i];
		if (!stat->addr) {
			break;
		}

		if (!stat->hello_count) {
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(top); j++) {
			if (top[j] < 0) {
				top[j] = i;
				break;
			}

			if (stat->hello_count <= stats[top[j]].hello_count) {
				continue;
			}

			/* Move other elements down the list */
			if (j < ARRAY_SIZE(top) - 1) {
				memmove(&top[j + 1], &top[j],
					((ARRAY_SIZE(top) - j - 1) *
					 sizeof(top[j])));
			}

			top[j] = i;
			break;
		}
	}

	if (stat_count > 0) {
		len = snprintk(str, sizeof(str), "Most messages from:");
		print_line(FONT_SMALL, line++, str, len, false);

		for (i = 0; i < ARRAY_SIZE(top); i++) {
			if (top[i] < 0) {
				break;
			}

			stat = &stats[top[i]];

			len = snprintk(str, sizeof(str), "%-3u 0x%04x %s",
				       stat->hello_count, stat->addr,
				       stat->name);
			print_line(FONT_SMALL, line++, str, len, false);
		}
	}

	cfb_framebuffer_finalize(display_dev);
}

static void show_sensors_data(k_timeout_t interval)
{
	struct sensor_value val[3];
	uint8_t line = 0U;
	uint16_t len = 0U;

	cfb_framebuffer_clear(display_dev, false);

	/* hdc1010 */
	if (get_hdc1010_val(val)) {
		goto _error_get;
	}

	len = snprintf(str_buf, sizeof(str_buf), "Temperature:%d.%d C\n",
		       val[0].val1, val[0].val2 / 100000);
	print_line(FONT_SMALL, line++, str_buf, len, false);

	len = snprintf(str_buf, sizeof(str_buf), "Humidity:%d%%\n",
		       val[1].val1);
	print_line(FONT_SMALL, line++, str_buf, len, false);

	/* mma8652 */
	if (get_mma8652_val(val)) {
		goto _error_get;
	}

	len = snprintf(str_buf, sizeof(str_buf), "AX :%10.3f\n",
		       sensor_value_to_double(&val[0]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	len = snprintf(str_buf, sizeof(str_buf), "AY :%10.3f\n",
		       sensor_value_to_double(&val[1]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	len = snprintf(str_buf, sizeof(str_buf), "AZ :%10.3f\n",
		       sensor_value_to_double(&val[2]));
	print_line(FONT_SMALL, line++, str_buf, len, false);

	/* apds9960 */
	if (get_apds9960_val(val)) {
		goto _error_get;
	}

	len = snprintf(str_buf, sizeof(str_buf), "Light :%d\n", val[0].val1);
	print_line(FONT_SMALL, line++, str_buf, len, false);
	len = snprintf(str_buf, sizeof(str_buf), "Proximity:%d\n", val[1].val1);
	print_line(FONT_SMALL, line++, str_buf, len, false);

	cfb_framebuffer_finalize(display_dev);

	k_delayed_work_submit(&display_work, interval);

	return;

	_error_get:
		printk("Failed to get sensor data or print a string\n");
}

static void show_main(void)
{
	char buf[CONFIG_BT_DEVICE_NAME_MAX];
	int i;

	strncpy(buf, bt_get_name(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Convert commas to newlines */
	for (i = 0; buf[i] != '\0'; i++) {
		if (buf[i] == ',') {
			buf[i] = '\n';
		}
	}

	board_show_text(buf, true, K_FOREVER);
}
//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//
//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//
//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//


#define OP_VND_HA         BT_MESH_MODEL_OP_3(OP_HA, BT_COMP_ID_LF)
#define HELLO_MAX         8
#define OP_HA		   0xbe
#define DEFAULT_TTL       31

static void my_data(k_timeout_t interval)
{
	uint8_t line = 0U;
	uint16_t len = 0U;

	cfb_framebuffer_clear(display_dev, true);

	const char* bluetooth_name = get_bluetooth_name();

	len = snprintf(str_buf, sizeof(str_buf), "*%s @%04x\n", bluetooth_name, mesh_get_addr());
	print_line(FONT_SMALL, line++, str_buf, len, false);

	for (int i = 0; i < current_nodes; i++)
	{
		len = snprintf(str_buf, sizeof(str_buf), "%s @%04x S:%d D:%.2f\n", 
			nodes_data[i].name, nodes_data[i].address, 
			nodes_data[i].last_rssi, nodes_data[i].estimated_distance);

		print_line(FONT_SMALL, line++, str_buf, len, false);
	}

	// Output average temperature
	len = snprintf(str_buf, sizeof(str_buf), "Avg. temperature: %.1f\n", average_node_temperature);
	print_line(FONT_SMALL, 6, str_buf, len, false);

	cfb_framebuffer_finalize(display_dev);
	k_delayed_work_submit(&display_work, interval);

	return;
}

//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//
//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//
//** this is my function. LOOK HERE LOOK HERE LOOK HERE **//



static void display_update(struct k_work *work)
{
	switch (screen_id) 
	{
		case MY_SCREEN:
			my_data(K_SECONDS(1));
			return;

		case SCREEN_STATS:
			show_statistics();
			return;

		case SCREEN_SENSORS:
			show_sensors_data(K_SECONDS(2));
			return;

		case SCREEN_MAIN:
			show_main();
			return;
	}
}

static void long_press(struct k_work *work)
{
	/* Treat as release so actual release doesn't send messages */
	pressed = false;
	screen_id = (screen_id + 1) % SCREEN_LAST;
	printk("Change screen to id = %d\n", screen_id);
	board_refresh_display();
}

static int get_temperature_value()
{
	struct sensor_value val[3];

	if (get_hdc1010_val(val))
	{
		printk("Couldn't get temperature value.\n");
		return -1;
	}

	return val[0].val1;
}

static int get_proximity_value()
{
	struct sensor_value val[3];

	if (get_apds9960_val(val))
	{
		printk("Couldn't get proximity value.\n");
		return -1;
	}

	return val[1].val1;
}

static void sensor_values_update(struct k_work *work)
{
	proximity = get_proximity_value();
	temperature = get_temperature_value();

	update_self_sensor_values(proximity, temperature);

	k_delayed_work_submit(&sensor_values_work, SENSOR_VALUES_REFRESH_INTERVAL);
}

static bool button_is_pressed(void)
{
	return gpio_pin_get(gpio, DT_GPIO_PIN(DT_ALIAS(sw0), gpios)) > 0;
}

static void button_interrupt(const struct device *dev,
			     struct gpio_callback *cb,
			     uint32_t pins)
{
	if (button_is_pressed() == pressed) {
		return;
	}

	pressed = !pressed;
	printk("Button %s\n", pressed ? "pressed" : "released");

	if (pressed) {
		k_delayed_work_submit(&long_press_work, LONG_PRESS_TIMEOUT);
		return;
	}

	k_delayed_work_cancel(&long_press_work);

	if (!mesh_is_initialized()) {
		return;
	}

	/* Short press for views */
	switch (screen_id) {
	case SCREEN_SENSORS:
	case SCREEN_STATS:
		return;
	case SCREEN_MAIN:
		if (pins & BIT(DT_GPIO_PIN(DT_ALIAS(sw0), gpios))) {
			uint32_t uptime = k_uptime_get_32();
			static uint32_t bad_count, press_ts;

			if (uptime - press_ts < 500) {
				bad_count++;
			} else {
				bad_count = 0U;
			}

			press_ts = uptime;

			if (bad_count) 
			{
				if (bad_count > 5) 
				{
					mesh_send_baduser();
					bad_count = 0U;
				} 
				else 
				{
					printk("Ignoring press\n");
				}
			} 
			else 
			{
				mesh_start_calibration(proximity);
			}
		}
		return;
	default:
		return;
	}
}

static int configure_button(void)
{
	static struct gpio_callback button_cb;

	gpio = device_get_binding(DT_GPIO_LABEL(DT_ALIAS(sw0), gpios));
	if (!gpio) {
		return -ENODEV;
	}

	gpio_pin_configure(gpio, DT_GPIO_PIN(DT_ALIAS(sw0), gpios),
			   GPIO_INPUT | DT_GPIO_FLAGS(DT_ALIAS(sw0), gpios));

	gpio_pin_interrupt_configure(gpio, DT_GPIO_PIN(DT_ALIAS(sw0), gpios),
				     GPIO_INT_EDGE_BOTH);

	gpio_init_callback(&button_cb, button_interrupt,
			   BIT(DT_GPIO_PIN(DT_ALIAS(sw0), gpios)));

	gpio_add_callback(gpio, &button_cb);

	return 0;
}

int set_led_state(uint8_t id, bool state)
{
	return gpio_pin_set(leds[id].dev, leds[id].pin, state);
}

static void led_timeout(struct k_work *work)
{
	static int led_cntr;
	int i;

	/* Disable all LEDs */
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		set_led_state(i, 0);
	}

	/* Stop after 5 iterations */
	if (led_cntr >= (ARRAY_SIZE(leds) * 5)) {
		led_cntr = 0;
		return;
	}

	/* Select and enable current LED */
	i = led_cntr++ % ARRAY_SIZE(leds);
	set_led_state(i, 1);

	k_delayed_work_submit(&led_timer, K_MSEC(100));
}

static int configure_leds(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].dev = device_get_binding(leds[i].name);
		if (!leds[i].dev) {
			printk("Failed to get %s device\n", leds[i].name);
			return -ENODEV;
		}

		gpio_pin_configure(leds[i].dev, leds[i].pin,
				   leds[i].flags |
				   GPIO_OUTPUT_INACTIVE);
	}

	k_delayed_work_init(&led_timer, led_timeout);
	return 0;
}

static int erase_storage(void)
{
	const struct device *dev;

	dev = device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);

	return flash_erase(dev, FLASH_AREA_OFFSET(storage),
			   FLASH_AREA_SIZE(storage));
}

void board_refresh_display(void)
{
	k_delayed_work_submit(&display_work, K_NO_WAIT);
}

void start_sensor_values_work()
{
	k_delayed_work_submit(&sensor_values_work, K_NO_WAIT);
}

int board_init(void)
{
	display_dev = device_get_binding(DT_LABEL(DT_INST(0, solomon_ssd16xxfb)));

	if (display_dev == NULL) 
	{
		printk("SSD16XX device not found\n");
		return -ENODEV;
	}

	if (cfb_framebuffer_init(display_dev)) 
	{
		printk("Framebuffer initialization failed\n");
		return -EIO;
	}

	cfb_framebuffer_clear(display_dev, true);

	if (configure_button()) 
	{
		printk("Failed to configure button\n");
		return -EIO;
	}

	if (configure_leds()) 
	{
		printk("LED init failed\n");
		return -EIO;
	}

	k_delayed_work_init(&display_work, display_update);
	k_delayed_work_init(&long_press_work, long_press);
	k_delayed_work_init(&sensor_values_work, sensor_values_update);

	pressed = button_is_pressed();

	if (pressed) 
	{
		printk("Erasing storage\n");
		board_show_text("Resetting Device", false, K_SECONDS(4));
		erase_storage();
	}

	return 0;
}
