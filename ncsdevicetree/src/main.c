/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>
#include <zephyr/drivers/i2c.h>
#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1//运行时闪
#define CON_STATUS_LED          DK_LED2//连接就亮
#define USER_LED_0              DK_LED3//写入1时亮 写入0时灭 
#define USER_LED_1			  	DK_LED5//我在dts中添加的一个灯

//服务的UUID：00001523-1212-efde-1523-785feabcd123  
//特征读UUID：00001524-1212-efde-1523-785feabcd123
//特征写UUID：00001525-1212-efde-1523-785feabcd123

#define RUN_LED_BLINK_INTERVAL  1000

#define USER_BUTTON             DK_BTN1_MSK

static bool app_button_state;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	dk_set_led_off(CON_STATUS_LED);
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED_0, led_state);
}

static bool app_button_cb(void)
{
	return app_button_state;
}

static struct bt_lbs_cb lbs_callbacs = {
	.led_cb    = app_led_cb,
	.button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		bt_lbs_send_button_state(user_button_state);
		app_button_state = user_button_state ? true : false;
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}


// 自己想要操作的节点的id，这里想要操作的节点是zephyr,user
#define NODE_ID DT_PATH(zephyr_user)

// 获取到zephyr,user节点的test-gpios属性，并把它作为gpio specifier，读入GPIO驱动。
static const struct gpio_dt_spec test_io = GPIO_DT_SPEC_GET(NODE_ID, test_gpios);



void i2c_init_and_transfer(void)
{
    struct device *i2c_dev;
    uint8_t buffer[2] = {0x01, 0x02};

	i2c_dev = device_get_binding("I2C_0");//device_get_binding("my-i2c");别名也是name

	//device_get_binding("/soc/i2c@40003000");/*/soc/i2c@40003000/*/
    if (!i2c_dev) {
        printk("Failed to get I2C device binding\n");
        return;
    }

    // 初始化I2C设备
    if (i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_STANDARD))) {
        printk("Failed to configure I2C device\n");
        return;
    }

    // 设置I2C传输消息
    struct i2c_msg msgs[1] = {
        {
            .buf = buffer,
            .len = sizeof(buffer),
            .flags = I2C_MSG_WRITE,
        },
    };

    // 发送I2C传输消息
    if (i2c_transfer(i2c_dev, msgs, 1, 0x12) < 0) {
        printk("Failed to transfer data over I2C\n");
        return;
    }

    // 读取I2C传输消息
    uint8_t read_buffer[2];
    struct i2c_msg read_msgs[1] = {
        {
            .buf = read_buffer,
            .len = sizeof(read_buffer),
            .flags = I2C_MSG_READ,
        },
    };

    if (i2c_transfer(i2c_dev, read_msgs, 1, 0x12) < 0) {
        printk("Failed to read data over I2C\n");
        return;
    }

    // 打印读取到的数据
    printk("Read data: %02x %02x\n", read_buffer[0], read_buffer[1]);
}

int main(void)
{
	int blink_status = 0;
	int err;

    // 判断设备（这里是gpio控制器）是否已初始化完毕
    // 一般情况下，在application运行前，zephyr驱动就已经把控制器初始化好了
    if (!device_is_ready(test_io.port)) {
        return;
    }
	// 重新配置IO
    // 如果DeviceTree里写好了，这里也可以不配
    gpio_pin_configure_dt(&test_io, GPIO_OUTPUT_INACTIVE);


	printk("Starting Bluetooth Peripheral LBS example\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_lbs_init(&lbs_callbacs);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return 0;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		dk_set_led(USER_LED_1, (blink_status) % 2);
    	gpio_pin_set_dt(&test_io,(blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
