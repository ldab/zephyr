/*
 * Copyright (c) 2025 Leonardo Bispo
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sinowealth SH8601 Display Controller Driver
 * Based on Espressif esp_lcd_sh8601 driver (Apache-2.0 license)
 */

#define DT_DRV_COMPAT sinowealth_sh8601

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sh8601, CONFIG_DISPLAY_LOG_LEVEL);

/* SH8601 commands */
#define SH8601_CMD_SWRESET     0x01U /* Software reset */
#define SH8601_CMD_SLPIN       0x10U /* Sleep in */
#define SH8601_CMD_SLPOUT      0x11U /* Sleep out */
#define SH8601_CMD_INVOFF      0x20U /* Invert off */
#define SH8601_CMD_INVON       0x21U /* Invert on */
#define SH8601_CMD_DISPOFF     0x28U /* Display off */
#define SH8601_CMD_DISPON      0x29U /* Display on */
#define SH8601_CMD_CASET       0x2AU /* Column address set */
#define SH8601_CMD_RASET       0x2BU /* Row address set */
#define SH8601_CMD_RAMWR       0x2CU /* Memory write */
#define SH8601_CMD_MADCTL      0x36U /* Memory access control */
#define SH8601_CMD_COLMOD      0x3AU /* Interface pixel format */
#define SH8601_CMD_SDIR        0xB7U /* Source driver direction */
#define SH8601_CMD_SS_REG      0xC5U /* Scanning direction control */

/* MADCTL bits */
#define SH8601_MADCTL_MY  BIT(7) /* Row address order */
#define SH8601_MADCTL_MX  BIT(6) /* Column address order */

struct sh8601_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec reset;
	uint16_t width;
	uint16_t height;
	bool mirror_x;
	bool mirror_y;
};

struct sh8601_data {
	uint8_t madctl;
};

static int sh8601_transmit_cmd(const struct device *dev, uint8_t cmd,
			       const void *tx_data, size_t tx_len)
{
	const struct sh8601_config *config = dev->config;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
	int ret;

	/* Send command byte */
	tx_buf.buf = &cmd;
	tx_buf.len = 1;
	ret = spi_write_dt(&config->bus, &tx_bufs);
	if (ret < 0) {
		LOG_ERR("Failed to send cmd 0x%02x: %d", cmd, ret);
		return ret;
	}

	/* Send parameters if provided */
	if (tx_data && tx_len > 0) {
		tx_buf.buf = (void *)tx_data;
		tx_buf.len = tx_len;
		ret = spi_write_dt(&config->bus, &tx_bufs);
		if (ret < 0) {
			LOG_ERR("Failed to send params: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int sh8601_transmit_pixels(const struct device *dev,
				  const void *buf, size_t len)
{
	const struct sh8601_config *config = dev->config;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
	uint8_t cmd = SH8601_CMD_RAMWR;
	int ret;

	/* Send RAMWR command */
	tx_buf.buf = &cmd;
	tx_buf.len = 1;
	ret = spi_write_dt(&config->bus, &tx_bufs);
	if (ret < 0) {
		LOG_ERR("Failed to send RAMWR: %d", ret);
		return ret;
	}

	/* Send pixel data */
	tx_buf.buf = (void *)buf;
	tx_buf.len = len;
	ret = spi_write_dt(&config->bus, &tx_bufs);
	if (ret < 0) {
		LOG_ERR("Failed to write pixels: %d", ret);
		return ret;
	}

	return 0;
}

static int sh8601_write(const struct device *dev, const uint16_t x,
		       const uint16_t y, const struct display_buffer_descriptor *desc,
		       const void *buf)
{
	uint8_t col_data[4];
	uint8_t row_data[4];
	uint16_t x_start = x;
	uint16_t x_end = x + desc->width - 1;
	uint16_t y_start = y;
	uint16_t y_end = y + desc->height - 1;
	int ret;

	/* Ensure coordinates are even (SH8601 hardware requirement) */
	x_start &= ~1;
	x_end |= 1;
	y_start &= ~1;
	y_end |= 1;

	/* Column address set */
	sys_put_be16(x_start, &col_data[0]);
	sys_put_be16(x_end, &col_data[2]);
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_CASET, col_data, sizeof(col_data));
	if (ret < 0) {
		LOG_ERR("Failed to set column address");
		return ret;
	}

	/* Row address set */
	sys_put_be16(y_start, &row_data[0]);
	sys_put_be16(y_end, &row_data[2]);
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_RASET, row_data, sizeof(row_data));
	if (ret < 0) {
		LOG_ERR("Failed to set row address");
		return ret;
	}

	/* Write pixel data */
	uint32_t pixel_len = desc->width * desc->height * 2; /* RGB565 = 2 bytes/pixel */
	ret = sh8601_transmit_pixels(dev, buf, pixel_len);
	if (ret < 0) {
		LOG_ERR("Failed to write pixels");
		return ret;
	}

	return 0;
}

static int sh8601_blanking_on(const struct device *dev)
{
	return sh8601_transmit_cmd(dev, SH8601_CMD_DISPOFF, NULL, 0);
}

static int sh8601_blanking_off(const struct device *dev)
{
	return sh8601_transmit_cmd(dev, SH8601_CMD_DISPON, NULL, 0);
}

static void *sh8601_get_framebuffer(const struct device *dev)
{
	return NULL;
}

static int sh8601_set_brightness(const struct device *dev, uint8_t brightness)
{
	return -ENOTSUP;
}

static int sh8601_set_contrast(const struct device *dev, uint8_t contrast)
{
	return -ENOTSUP;
}

static int sh8601_set_pixel_format(const struct device *dev,
				   enum display_pixel_format pixel_format)
{
	if (pixel_format != PIXEL_FORMAT_RGB_565) {
		return -ENOTSUP;
	}
	return 0;
}

static int sh8601_set_orientation(const struct device *dev,
				  enum display_orientation orientation)
{
	return -ENOTSUP;
}

static void sh8601_get_capabilities(const struct device *dev,
				    struct display_capabilities *capabilities)
{
	const struct sh8601_config *config = dev->config;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
	capabilities->screen_info = SCREEN_INFO_MONO_VTILED;
}

static const struct display_driver_api sh8601_api = {
	.blanking_on = sh8601_blanking_on,
	.blanking_off = sh8601_blanking_off,
	.write = sh8601_write,
	.get_framebuffer = sh8601_get_framebuffer,
	.set_brightness = sh8601_set_brightness,
	.set_contrast = sh8601_set_contrast,
	.set_pixel_format = sh8601_set_pixel_format,
	.set_orientation = sh8601_set_orientation,
	.get_capabilities = sh8601_get_capabilities,
};

static int sh8601_init(const struct device *dev)
{
	const struct sh8601_config *config = dev->config;
	struct sh8601_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	/* Configure reset GPIO if present */
	if (config->reset.port) {
		ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure reset GPIO");
			return ret;
		}

		/* Perform hardware reset */
		gpio_pin_set_dt(&config->reset, 1);
		k_msleep(10);
		gpio_pin_set_dt(&config->reset, 0);
		k_msleep(10);
		gpio_pin_set_dt(&config->reset, 1);
		k_msleep(120);
	}

	/* Software reset */
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_SWRESET, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send software reset");
		return ret;
	}
	k_msleep(120);

	/* Sleep out */
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_SLPOUT, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send SLPOUT command");
		return ret;
	}
	k_msleep(120);

	/* SDIR - Source Driver Direction */
	uint8_t sdir_param[] = {0x04};
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_SDIR, sdir_param, sizeof(sdir_param));
	if (ret < 0) {
		LOG_ERR("Failed to configure source driver direction");
		return ret;
	}

	/* SS_REG - Scanning direction control */
	uint8_t ss_param[] = {0x4F, 0x2D};
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_SS_REG, ss_param, sizeof(ss_param));
	if (ret < 0) {
		LOG_ERR("Failed to configure scanning direction");
		return ret;
	}

	/* COLMOD - Interface pixel format (0x55 = RGB565) */
	uint8_t colmod_param[] = {0x55};
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_COLMOD, colmod_param, sizeof(colmod_param));
	if (ret < 0) {
		LOG_ERR("Failed to set color mode");
		return ret;
	}

	/* MADCTL - Memory Access Control */
	data->madctl = 0x00;
	if (config->mirror_x) {
		data->madctl |= SH8601_MADCTL_MX;
	}
	if (config->mirror_y) {
		data->madctl |= SH8601_MADCTL_MY;
	}
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_MADCTL, &data->madctl, 1);
	if (ret < 0) {
		LOG_ERR("Failed to set memory access control");
		return ret;
	}

	/* Display invert off */
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_INVOFF, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send INVOFF command");
		return ret;
	}

	/* Display on */
	ret = sh8601_transmit_cmd(dev, SH8601_CMD_DISPON, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send DISPON command");
		return ret;
	}

	LOG_INF("SH8601 initialized");
	return 0;
}

#define SH8601_INIT(inst)							\
	static struct sh8601_data sh8601_data_##inst;			\
									\
	static const struct sh8601_config sh8601_config_##inst = {	\
		.bus = SPI_DT_SPEC_INST_GET(inst,			\
					    SPI_OP_MODE_MASTER |	\
					    SPI_WORD_SET(8) |		\
					    SPI_TRANSFER_MSB |		\
					    SPI_HOLD_ON_CS),		\
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, \
					    {0}),			\
	.width = DT_INST_PROP(inst, width),                \
	.height = DT_INST_PROP(inst, height),               \
		.mirror_x = DT_PROP_OR(DT_DRV_INST(inst),		\
				       mirror_x, false),		\
		.mirror_y = DT_PROP_OR(DT_DRV_INST(inst),		\
				       mirror_y, false),		\
	};								\
									\
	DEVICE_DT_INST_DEFINE(inst, sh8601_init, NULL,			\
			      &sh8601_data_##inst,			\
			      &sh8601_config_##inst, POST_KERNEL,	\
			      CONFIG_DISPLAY_INIT_PRIORITY,		\
			      &sh8601_api);

DT_INST_FOREACH_STATUS_OKAY(SH8601_INIT)

