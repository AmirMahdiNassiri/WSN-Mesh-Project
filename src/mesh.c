#include <stdio.h>
#include <zephyr.h>
#include <string.h>
#include <stdlib.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh.h>
#include <bluetooth/hci.h>

#include <drivers/sensor.h>

#include "mesh.h"
#include "board.h"
#include "distance_estimator.h"

// ======================================== CONST Configurations ======================================== //

#define MOD_LF            0x0000
#define OP_CALIBRATION          0xbb
#define OP_HEARTBEAT      0xbc
#define OP_BADUSER        0xbd
#define OP_VND_CALIBRATION      BT_MESH_MODEL_OP_3(OP_CALIBRATION, BT_COMP_ID_LF)
#define OP_VND_HEARTBEAT  BT_MESH_MODEL_OP_3(OP_HEARTBEAT, BT_COMP_ID_LF)
#define OP_VND_BADUSER    BT_MESH_MODEL_OP_3(OP_BADUSER, BT_COMP_ID_LF)

#define IV_INDEX          0
#define DEFAULT_TTL       31
#define GROUP_ADDR        0xc123
#define NET_IDX           0x000
#define APP_IDX           0x000
#define FLAGS             0

#define TTL_SIZE 1
#define NAME_SIZE         8
#define PROXIMITY_SIZE 4
#define TEMPERATURE_SIZE 4

#define VALID_PROXIMITY_DELTA 10

#define MAX_SENS_STATUS_LEN 8

#define SENS_PROP_ID_PRESENT_DEVICE_TEMP 0x0054

enum 
{
	SENSOR_HDR_A = 0,
	SENSOR_HDR_B = 1,
};

struct sensor_hdr_a {
	uint16_t prop_id:11;
	uint16_t length:4;
	uint16_t format:1;
} __packed;

struct sensor_hdr_b {
	uint8_t length:7;
	uint8_t format:1;
	uint16_t prop_id;
} __packed;

static struct k_work calibration_work;
static struct k_work baduser_work;
static struct k_work mesh_start_work;

/* Definitions of models user data (Start) */
static struct led_onoff_state led_onoff_state[] = {
	/* Use LED 0 for this model */
	{ .dev_id = 0 },
};

const char* get_bluetooth_name()
{
	return bt_get_name();
}

void copy_bluetooth_name(char *buffer)
{
	const char* bluetooth_name = get_bluetooth_name();
	strcpy(self_node_data.name, bluetooth_name);
}

static void heartbeat(uint8_t hops, uint16_t feat)
{
	board_show_text("Heartbeat Received", false, K_SECONDS(2));
}

static struct bt_mesh_cfg_srv cfg_srv = {
	.relay = BT_MESH_RELAY_ENABLED,
	.beacon = BT_MESH_BEACON_DISABLED,
	.default_ttl = DEFAULT_TTL,

	/* 3 transmissions with 20ms interval */
	.net_transmit = BT_MESH_TRANSMIT(2, 20),
	.relay_retransmit = BT_MESH_TRANSMIT(3, 20),

	.hb_sub.func = heartbeat,
};

static struct bt_mesh_cfg_cli cfg_cli = { };

static void attention_on(struct bt_mesh_model *model)
{
	board_show_text("Attention!", false, K_SECONDS(2));
}

static void attention_off(struct bt_mesh_model *model)
{
	board_refresh_display();
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

/* Generic OnOff Server message handlers */
static void gen_onoff_get(struct bt_mesh_model *model,
			  struct bt_mesh_msg_ctx *ctx,
			  struct net_buf_simple *buf)
{
	NET_BUF_SIMPLE_DEFINE(msg, 2 + 1 + 4);
	struct led_onoff_state *state = model->user_data;

	printk("addr 0x%04x onoff 0x%02x\n",
	       bt_mesh_model_elem(model)->addr, state->current);
	bt_mesh_model_msg_init(&msg, BT_MESH_MODEL_OP_GEN_ONOFF_STATUS);
	net_buf_simple_add_u8(&msg, state->current);

	if (bt_mesh_model_send(model, ctx, &msg, NULL, NULL)) {
		printk("Unable to send On Off Status response\n");
	}
}

static void gen_onoff_set_unack(struct bt_mesh_model *model,
				struct bt_mesh_msg_ctx *ctx,
				struct net_buf_simple *buf)
{
	struct net_buf_simple *msg = model->pub->msg;
	struct led_onoff_state *state = model->user_data;
	int err;
	uint8_t tid, onoff;
	int64_t now;

	onoff = net_buf_simple_pull_u8(buf);
	tid = net_buf_simple_pull_u8(buf);

	if (onoff > STATE_ON) {
		printk("Wrong state received\n");

		return;
	}

	now = k_uptime_get();
	if (state->last_tid == tid && state->last_tx_addr == ctx->addr &&
	    (now - state->last_msg_timestamp <= (6 * MSEC_PER_SEC))) {
		printk("Already received message\n");
	}

	state->current = onoff;
	state->last_tid = tid;
	state->last_tx_addr = ctx->addr;
	state->last_msg_timestamp = now;

	printk("addr 0x%02x state 0x%02x\n",
	       bt_mesh_model_elem(model)->addr, state->current);

	if (set_led_state(state->dev_id, onoff)) {
		printk("Failed to set led state\n");

		return;
	}

	/*
	 * If a server has a publish address, it is required to
	 * publish status on a state change
	 *
	 * See Mesh Profile Specification 3.7.6.1.2
	 *
	 * Only publish if there is an assigned address
	 */

	if (state->previous != state->current &&
	    model->pub->addr != BT_MESH_ADDR_UNASSIGNED) {
		printk("publish last 0x%02x cur 0x%02x\n",
		       state->previous, state->current);
		state->previous = state->current;
		bt_mesh_model_msg_init(msg,
				       BT_MESH_MODEL_OP_GEN_ONOFF_STATUS);
		net_buf_simple_add_u8(msg, state->current);
		err = bt_mesh_model_publish(model);
		if (err) {
			printk("bt_mesh_model_publish err %d\n", err);
		}
	}
}

static void gen_onoff_set(struct bt_mesh_model *model,
			  struct bt_mesh_msg_ctx *ctx,
			  struct net_buf_simple *buf)
{
	gen_onoff_set_unack(model, ctx, buf);
	gen_onoff_get(model, ctx, buf);
}

static void sensor_desc_get(struct bt_mesh_model *model,
			    struct bt_mesh_msg_ctx *ctx,
			    struct net_buf_simple *buf)
{
	/* TODO */
}

static void sens_temperature_celsius_fill(struct net_buf_simple *msg)
{
	struct sensor_hdr_a hdr;
	/* TODO Get only temperature from sensor */
	struct sensor_value val[2];
	int16_t temp_degrees;

	hdr.format = SENSOR_HDR_A;
	hdr.length = sizeof(temp_degrees);
	hdr.prop_id = SENS_PROP_ID_PRESENT_DEVICE_TEMP;

	get_hdc1010_val(val);
	temp_degrees = sensor_value_to_double(&val[0]) * 100;
	printf("The temperture value :%d",temp_degrees);
	net_buf_simple_add_mem(msg, &hdr, sizeof(hdr));
	net_buf_simple_add_le16(msg, temp_degrees);
}

static void sens_unknown_fill(uint16_t id, struct net_buf_simple *msg)
{
	struct sensor_hdr_b hdr;

	/*
	 * When the message is a response to a Sensor Get message that
	 * identifies a sensor property that does not exist on the element, the
	 * Length field shall represent the value of zero and the Raw Value for
	 * that property shall be omitted. (Mesh model spec 1.0, 4.2.14).
	 *
	 * The length zero is represented using the format B and the special
	 * value 0x7F.
	 */
	hdr.format = SENSOR_HDR_B;
	hdr.length = 0x7FU;
	hdr.prop_id = id;

	net_buf_simple_add_mem(msg, &hdr, sizeof(hdr));
}

static void sensor_create_status(uint16_t id, struct net_buf_simple *msg)
{
	bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_SENS_STATUS);

	switch (id) {
	case SENS_PROP_ID_PRESENT_DEVICE_TEMP:
		sens_temperature_celsius_fill(msg);
		break;
	default:
		sens_unknown_fill(id, msg);
		break;
	}
}

static void sensor_get(struct bt_mesh_model *model,
		       struct bt_mesh_msg_ctx *ctx,
		       struct net_buf_simple *buf)
{
	NET_BUF_SIMPLE_DEFINE(msg, 1 + MAX_SENS_STATUS_LEN + 4);
	uint16_t sensor_id;
	printk("Senor_get function\n");
	sensor_id = net_buf_simple_pull_le16(buf);
	sensor_create_status(sensor_id, &msg);

	if (bt_mesh_model_send(model, ctx, &msg, NULL, NULL)) {
		printk("Unable to send Sensor get status response\n");
	}
}

static void sensor_col_get(struct bt_mesh_model *model,
			   struct bt_mesh_msg_ctx *ctx,
			   struct net_buf_simple *buf)
{
	/* TODO */
}

static void sensor_series_get(struct bt_mesh_model *model,
			      struct bt_mesh_msg_ctx *ctx,
			      struct net_buf_simple *buf)
{
	/* TODO */
}

static int sensor_pub_update(struct bt_mesh_model *mod){
	struct net_buf_simple *msg = mod->pub->msg;

	printk("Preparing to send heartbeat\n");

	bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_SENS_GET);
	
	net_buf_simple_add_u8(msg, DEFAULT_TTL);

	net_buf_simple_add_u8(msg, DEFAULT_TTL);

	return 0;
}

/* Definitions of models publication context (Start) */
BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);
BT_MESH_MODEL_PUB_DEFINE(gen_onoff_srv_pub_root, NULL, 2 + 3);
BT_MESH_MODEL_PUB_DEFINE(sensor_srv_pub, sensor_pub_update, 3 + 1);
/* Mapping of message handlers for Generic OnOff Server (0x1000) */
static const struct bt_mesh_model_op gen_onoff_srv_op[] = {
	{ BT_MESH_MODEL_OP_GEN_ONOFF_GET, 0, gen_onoff_get },
	{ BT_MESH_MODEL_OP_GEN_ONOFF_SET, 2, gen_onoff_set },
	{ BT_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK, 2, gen_onoff_set_unack },
	BT_MESH_MODEL_OP_END,
};

/* Mapping of message handlers for Sensor Server (0x1100) */
static const struct bt_mesh_model_op sensor_srv_op[] = {
	{ BT_MESH_MODEL_OP_SENS_DESC_GET, 0, sensor_desc_get },
	{ BT_MESH_MODEL_OP_SENS_GET, 1, sensor_get },
	{ BT_MESH_MODEL_OP_SENS_COL_GET, 2, sensor_col_get },
	{ BT_MESH_MODEL_OP_SENS_SERIES_GET, 2, sensor_series_get },
};

static struct bt_mesh_model root_models[] = 
{
	BT_MESH_MODEL_CFG_SRV(&cfg_srv),
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV,
		      gen_onoff_srv_op, &gen_onoff_srv_pub_root,
		      &led_onoff_state[0]),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_SENSOR_SRV,
		      sensor_srv_op, &sensor_srv_pub, NULL),
};

int is_in_vicinity(int other_node_proximity)
{
	if (abs(other_node_proximity - self_node_data.proximity) < VALID_PROXIMITY_DELTA)
		return 1;

	return 0;
}

// Calibration handler
static void vnd_calibration(struct bt_mesh_model *model,
			struct bt_mesh_msg_ctx *ctx,
			struct net_buf_simple *buf)
{
	if (ctx->addr == bt_mesh_model_elem(model)->addr) 
	{
		printk("Ignoring calibration from self.\n");
		return;
	}

	// Fetch proximity
	int received_proximity;
	memcpy(&received_proximity, buf->data, PROXIMITY_SIZE);

	// Fetch name
	char received_name[32];
	size_t len = MIN(buf->len - PROXIMITY_SIZE, NAME_SIZE);

	memcpy(received_name, buf->data + PROXIMITY_SIZE, len);
	received_name[len] = '\0';

	printk("Calibration received. Address: 0x%04x Name: %s RSSI: %04d Proximity: %d\n", 
		ctx->addr, received_name, ctx->recv_rssi, received_proximity);

	// Handle calibration if in correct vicinity
	if (is_in_vicinity(received_proximity))
	{
		printk("Node is in the right vicinity, attempting to calibrate.\n");

		int result = calibrate_node(ctx->addr, received_name, received_proximity, ctx->recv_rssi);

		if (result == 1)
		{
			printk("First calibration, attempting send calibrate for the other node.\n");
			k_work_submit(&calibration_work);
			board_blink_leds();
		}
		else if (result == 0)
			printk("Repeating calibration successful.\n");
		else
			printk("Calibration failed: %d\n", result);
	}

	// Handle the name
	board_add_hello(ctx->addr, received_name);
	board_show_text(received_name, false, K_SECONDS(1));
}

// Baduser message handler
static void vnd_baduser(struct bt_mesh_model *model,
			struct bt_mesh_msg_ctx *ctx,
			struct net_buf_simple *buf)
{
	char str[32];
	size_t len;

	printk("\"Bad user\" message from 0x%04x\n", ctx->addr);

	if (ctx->addr == bt_mesh_model_elem(model)->addr) {
		printk("Ignoring bad user from self.\n");
		return;
	}

	len = MIN(buf->len, NAME_SIZE);
	memcpy(str, buf->data, len);
	str[len] = '\0';

	strcat(str, " is misbehaving!");
	board_show_text(str, false, K_SECONDS(2));

	board_blink_leds();
}

// Heartbeat message handler
static void vnd_heartbeat(struct bt_mesh_model *model,
			struct bt_mesh_msg_ctx *ctx,
			struct net_buf_simple *buf)
{
	uint8_t init_ttl, hops;

	if (ctx->addr == bt_mesh_model_elem(model)->addr) 
	{
		self_node_data.address = ctx->addr;
		printk("Ignoring heartbeat from self.\n");

		return;
	}

	init_ttl = net_buf_simple_pull_u8(buf);
	hops = init_ttl - ctx->recv_ttl + 1;

	printk("Heartbeat from 0x%04x rssi %d size %d over %u hop%s.\n", 
		ctx->addr, ctx->recv_rssi, buf->len, hops, hops == 1U ? "" : "s");

	char message[MAX_MESSAGE_SIZE];

	memcpy(message, buf->data, buf->len);
	message[(buf->len - TTL_SIZE) + 1] = '\0';

	printf("Received message: '%s'\n", message);

	update_node_data(ctx->addr, ctx->recv_rssi, message);

	board_add_heartbeat(ctx->addr, hops);
}

// Vendor model operations
static const struct bt_mesh_model_op vnd_ops[] = 
{
	{ OP_VND_CALIBRATION, 1, vnd_calibration },
	{ OP_VND_HEARTBEAT, 1, vnd_heartbeat },
	{ OP_VND_BADUSER, 1, vnd_baduser },
	BT_MESH_MODEL_OP_END,
};

// Publish message update
static int vnd_pub_update(struct bt_mesh_model *mod)
{
	struct net_buf_simple *msg = mod->pub->msg;

	printk("Preparing to send vendor heartbeat\n");

	bt_mesh_model_msg_init(msg, OP_VND_HEARTBEAT);
	//bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_SENS_GET);
	net_buf_simple_add_u8(msg, DEFAULT_TTL);

	char* message = (char*) malloc(MAX_MESSAGE_SIZE * sizeof(char));

	if (message == NULL)
	{
		printf("Publication canceled: Couldn't allocate message memory.");
		return -1;
	}
	else
	{
		get_self_node_message(message);
		printf("Outgoing message with size %lu: '%s'\n", (long unsigned int) strlen(message), message);

		net_buf_simple_add_mem(msg, message, strlen(message));
		free(message);
		
		return 0;
	}
}

// Define publish model
BT_MESH_MODEL_PUB_DEFINE(vnd_pub, vnd_pub_update, 3 + 1);

// Element vendor models
static struct bt_mesh_model vnd_models[] = 
{
	BT_MESH_MODEL_VND(BT_COMP_ID_LF, MOD_LF, vnd_ops, &vnd_pub, NULL),
};

// Node elements
static struct bt_mesh_elem elements[] = 
{
	BT_MESH_ELEM(0, root_models, vnd_models),
};

// Node Composition
static const struct bt_mesh_comp comp = 
{
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static size_t first_name_len(const char *name)
{
	size_t len;

	for (len = 0; *name; name++, len++) {
		switch (*name) {
		case ' ':
		case ',':
		case '\n':
			return len;
		}
	}

	return len;
}

static void send_calibration(struct k_work *work)
{
	printk("Attempting to send_calibration with %d proximity value.\n", self_node_data.proximity);

	if (is_valid_calibration(self_node_data.proximity))
	{
		NET_BUF_SIMPLE_DEFINE(msg, 3 + PROXIMITY_SIZE + NAME_SIZE + 4);

		struct bt_mesh_msg_ctx ctx = 
		{
			.app_idx = APP_IDX,
			.addr = GROUP_ADDR,
			.send_ttl = DEFAULT_TTL,
		};

		// Initialize message
		bt_mesh_model_msg_init(&msg, OP_VND_CALIBRATION);

		// Add proximity data
		net_buf_simple_add_mem(&msg, &self_node_data.proximity, PROXIMITY_SIZE);

		// Add bluetooth name
		const char* bluetooth_name = get_bluetooth_name();
		net_buf_simple_add_mem(&msg, bluetooth_name, MIN(NAME_SIZE, first_name_len(bluetooth_name)));

		if (bt_mesh_model_send(&vnd_models[0], &ctx, &msg, NULL, NULL) == 0) 
		{
			board_show_text("Sending calibration", false, K_SECONDS(1));
		} 
		else 
		{
			board_show_text("Sending failed!", false, K_SECONDS(1));
		}
	}
	else
	{
		// No board in the right vicinity found
		printk("Bad proximity for calibration (p=%d). Proximity should be in the range (%d<p<%d) for calibration.\n", 
			self_node_data.proximity, CALIBRATION_START_MIN, CALIBRATION_START_MAX);

		char str_buf[256];

		snprintf(str_buf, sizeof(str_buf), "! prox=%d ! (%d<p<%d)", self_node_data.proximity,
			CALIBRATION_START_MIN, CALIBRATION_START_MAX);

		board_show_text(str_buf, false, K_SECONDS(1));
	}
}

void mesh_send_calibration()
{
	k_work_submit(&calibration_work);
}

static void send_baduser(struct k_work *work)
{
	NET_BUF_SIMPLE_DEFINE(msg, 3 + NAME_SIZE + 4);

	struct bt_mesh_msg_ctx ctx = 
	{
		.app_idx = APP_IDX,
		.addr = GROUP_ADDR,
		.send_ttl = DEFAULT_TTL,
	};

	const char* bluetooth_name = get_bluetooth_name();
	
	bt_mesh_model_msg_init(&msg, OP_VND_BADUSER);
	net_buf_simple_add_mem(&msg, bluetooth_name, MIN(NAME_SIZE, first_name_len(bluetooth_name)));

	if (bt_mesh_model_send(&vnd_models[0], &ctx, &msg, NULL, NULL) == 0) 
	{
		board_show_text("Bad user!", false, K_SECONDS(2));
	} 
	else 
	{
		board_show_text("Sending Failed!", false, K_SECONDS(2));
	}
}

void mesh_send_baduser(void)
{
	k_work_submit(&baduser_work);
}

static int provision_and_configure(void)
{
	static const uint8_t net_key[16] = {
		0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
		0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
	};

	static const uint8_t app_key[16] = {
		0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
		0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
	};

	struct bt_mesh_cfg_mod_pub pub = {
		.addr = GROUP_ADDR,
		.app_idx = APP_IDX,
		.ttl = DEFAULT_TTL,
		.period = BT_MESH_PUB_PERIOD_SEC(10),
	};

	uint8_t dev_key[16];
	uint16_t addr;
	int err;

	err = bt_rand(dev_key, sizeof(dev_key));

	if (err) 
		return err;

	do 
	{
		err = bt_rand(&addr, sizeof(addr));

		if (err)
			return err;
			
	} while (!addr);

	/* Make sure it's a unicast address (highest bit unset) */
	addr &= ~0x8000;

	err = bt_mesh_provision(net_key, NET_IDX, FLAGS, IV_INDEX, addr,
				dev_key);

	if (err)
		return err;

	printk("Configuring...\n");

	/* Add Application Key */
	bt_mesh_cfg_app_key_add(NET_IDX, addr, NET_IDX, APP_IDX, app_key, NULL);

	/* Bind to vendor model */
	bt_mesh_cfg_mod_app_bind_vnd(NET_IDX, addr, addr, APP_IDX,
					MOD_LF, BT_COMP_ID_LF, NULL);

	bt_mesh_cfg_mod_app_bind(NET_IDX, addr, addr, APP_IDX,
					BT_MESH_MODEL_ID_GEN_ONOFF_SRV, NULL);

	bt_mesh_cfg_mod_app_bind(NET_IDX, addr, addr, APP_IDX,
					BT_MESH_MODEL_ID_SENSOR_SRV, NULL);

	/* Bind to Health model */
	bt_mesh_cfg_mod_app_bind(NET_IDX, addr, addr, APP_IDX,
					BT_MESH_MODEL_ID_HEALTH_SRV, NULL);

	/* Add model subscription */
	bt_mesh_cfg_mod_sub_add_vnd(NET_IDX, addr, addr, GROUP_ADDR,
					MOD_LF, BT_COMP_ID_LF, NULL);

	bt_mesh_cfg_mod_pub_set_vnd(NET_IDX, addr, addr, MOD_LF, BT_COMP_ID_LF,
				    &pub, NULL);

	printk("Configuration complete\n");
	// printk("Hello message from 0x%04x \n", addr);

	return addr;
}

static void start_mesh(struct k_work *work)
{
	int err;

	err = provision_and_configure();
	if (err < 0) {
		board_show_text("Starting Mesh Failed", false,
				K_SECONDS(2));
	} else {
		char buf[32];

		snprintk(buf, sizeof(buf),
			 "Mesh Started\nAddr: 0x%04x", err);
		board_show_text(buf, false, K_SECONDS(4));
	}
}

void mesh_start(void)
{
	k_work_submit(&mesh_start_work);
}

bool mesh_is_initialized(void)
{
	return elements[0].addr != BT_MESH_ADDR_UNASSIGNED;
}

uint16_t mesh_get_addr(void)
{
	return elements[0].addr;
}

int mesh_init(void)
{
	static const uint8_t dev_uuid[16] = { 0xc0, 0xff, 0xee };
	static const struct bt_mesh_prov prov = {
		.uuid = dev_uuid,
	};

	k_work_init(&calibration_work, send_calibration);
	k_work_init(&baduser_work, send_baduser);
	k_work_init(&mesh_start_work, start_mesh);

	initialize_estimator();
	printk("Distance estimator initialized.\n");

	return bt_mesh_init(&prov, &comp);
}
