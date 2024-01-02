/*
 * ps5280.c
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <soc/gpio.h>
#include <tx-isp-common.h>
#include <sensor-common.h>

#define PS5280_CHIP_ID_H	(0x52)
#define PS5280_CHIP_ID_L	(0x80)
#define PS5280_REG_END		0xff
#define PS5280_REG_DELAY	0xfe
#define PS5280_BANK_REG		0xef

#define PS5280_SUPPORT_PCLK (81000000)
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define AG_HS_NODE	(40)	// 6.0x
#define AG_LS_NODE	(36)	// 5.0x
#define NEPLS_LB	(50)
#define NEPLS_UB	(250)
#define NEPLS_SCALE	(38)
#define NE_NEP_CONST_LINEAR	(0x708+0x32)
#define NE_NEP_CONST_WDR	(0xfa0+0x32)

#define SENSOR_VERSION	"H20170911a"

typedef enum {
	SENSOR_RAW_MODE_LINEAR = 0,
	SENSOR_RAW_MODE_NATIVE_WDR,
} supported_sensor_mode;

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func = DVP_PA_12BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

static int sensor_raw_mode = SENSOR_RAW_MODE_NATIVE_WDR;
module_param(sensor_raw_mode, int, S_IRUGO);
MODULE_PARM_DESC(sensor_raw_mode, "Sensor Raw Mode");

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut ps5280_again_lut[] = {
	{0, 0},
	{1, 5731},
	{2, 11136},
	{3, 16247},
	{4, 21097},
	{5, 25710},
	{6, 30108},
	{7, 34311},
	{8, 38335},
	{9, 42195},
	{10, 45903},
	{11, 49471},
	{12, 52910},
	{13, 56227},
	{14, 59433},
	{15, 62533},
	{16, 65535},
	{17, 71266},
	{18, 76671},
	{19, 81782},
	{20, 86632},
	{21, 91245},
	{22, 95643},
	{23, 99846},
	{24, 103870},
	{25, 107730},
	{26, 111438},
	{27, 115006},
	{28, 118445},
	{29, 121762},
	{30, 124968},
	{31, 128068},
	{32, 131070},
	{33, 136801},
	{34, 142206},
	{35, 147317},
	{36, 152167},
	{37, 156780},
	{38, 161178},
	{39, 165381},
	{40, 169405},
	{41, 173265},
	{42, 176973},
	{43, 180541},
	{44, 183980},
	{45, 187297},
	{46, 190503},
	{47, 193603},
	{48, 196605},
	{49, 202336},
	{50, 207741},
	{51, 212852},
	{52, 217702},
	{53, 222315},
	{54, 226713},
	{55, 230916},
	{56, 234940},
	{57, 238800},
	{58, 242508},
	{59, 246076},
	{60, 249515},
	{61, 252832},
	{62, 256038},
	{63, 259138},
	{64, 262140},
	{65, 267871},
	{66, 273276},
	{67, 278387},
	{68, 283237},
	{69, 287850},
	{70, 292248},
	{71, 296451},
	{72, 300475},
	{73, 304335},
	{74, 308043},
	{75, 311611},
	{76, 315050},
	{77, 318367},
	{78, 321573},
	{79, 324673},
	{80, 327675},
	{81, 333412},
	{82, 338816},
	{83, 343928},
	{84, 348778},
	{85, 353391},
	{86, 357789},
	{87, 361992},
	{88, 366016},
	{89, 369876},
	{90, 373584},
	{91, 377152},
	{92, 380591},
	{93, 383909},
	{94, 387114},
	{95, 390214},
	{96, 393216},
	{97, 398948},
	{98, 404352},
	{99, 409464},
	{100, 414314},
	{101, 418927},
	{102, 423325},
	{103, 427528},
	{104, 431552},
	{105, 435412},
	{106, 439120},
	{107, 442688},
	{108, 446127},
	{109, 449445},
	{110, 452650},
	{111, 455750},
	{112, 458752},
};

struct tx_isp_sensor_attribute ps5280_attr;

unsigned int ps5280_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = ps5280_again_lut;
	while (lut->gain <= ps5280_attr.max_again) {
		if (isp_gain <= ps5280_again_lut[0].gain) {
			*sensor_again = lut[0].value;
			return lut[0].gain;
		} else if (isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		} else {
			if((lut->gain == ps5280_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int ps5280_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return isp_gain;
}

struct tx_isp_sensor_attribute ps5280_attr={
	.name = "ps5280",
	.chip_id = 0x5280,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x48,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
	},
	.max_again = 458752,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 1436,
	.integration_time_limit = 1436,
	.total_width = 2250,
	.total_height = 1440,
	.max_integration_time = 1436,
	.one_line_expr_in_us = 30,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = ps5280_alloc_again,
	.sensor_ctrl.alloc_dgain = ps5280_alloc_dgain,
};


static struct regval_list ps5280_init_regs_1920_1080_linear_25fps[] = {
	{0xEF, 0x00},
	{0x0E, 0x45},
	{0x11, 0x00},
	{0x16, 0xBC},
	{0x36, 0x07},
	{0x37, 0x07},
	{0x38, 0x10},
	{0x63, 0x01},
	{0x64, 0x90},
	{0x67, 0x04},
	{0x68, 0xA0},
	{0x69, 0x10},
	{0x81, 0xC4},
	{0x85, 0x78},
	{0x9E, 0x40},
	{0xA3, 0x04},
	{0xA4, 0x68},
	{0xBE, 0x15},
	{0xDE, 0x03},
	{0xDF, 0x34},
	{0xE0, 0x10},
	{0xE1, 0x0A},
	{0xE2, 0x09},
	{0xE3, 0x34},
	{0xE4, 0x10},
	{0xE5, 0x0A},
	{0xE6, 0x09},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0x04, 0x10},
	{0x05, 0x01},
	{0x0A, 0x05},
	{0x0B, 0x9f},/*Lpf1*/
	{0x0C, 0x00},
	{0x0D, 0x04},
	{0x0E, 0x07},
	{0x0F, 0x08},
	{0x10, 0xC0},
	{0x11, 0x4B},
	{0x19, 0x40},
	{0x2E, 0xB4},
	{0x32, 0x1E},
	{0x33, 0x62},
	{0x37, 0x80},
	{0x39, 0x88},
	{0x3A, 0x1E},
	{0x3B, 0x6A},
	{0x3E, 0x11},
	{0x3F, 0xC8},
	{0x40, 0x1E},
	{0x42, 0xE8},
	{0x43, 0x62},
	{0x5D, 0x08},
	{0x5F, 0x32},
	{0x65, 0x1E},
	{0x66, 0x62},
	{0x78, 0x00},
	{0x7A, 0x00},
	{0x7B, 0x98},
	{0x7C, 0x07},
	{0x7D, 0xA0},
	{0x8F, 0x00},
	{0x90, 0x00},
	{0x96, 0x80},
	{0xA3, 0x00},
	{0xA4, 0x12},
	{0xA5, 0x04},
	{0xA6, 0x38},
	{0xA7, 0x00},
	{0xA8, 0x08},
	{0xA9, 0x07},
	{0xAA, 0x80},
	{0xAB, 0x02},
	{0x27, 0x08},
	{0x28, 0xCA},
	{0xB6, 0x07},
	{0xB7, 0x08},
	{0xB8, 0x00},
	{0xB9, 0x01},
	{0xBD, 0x02},
	{0xC9, 0x54},
	{0xCF, 0xBB},
	{0xD2, 0x22},
	{0xD3, 0x3E},
	{0xD4, 0xA4},
	{0xD6, 0x07},
	{0xD7, 0x0A},
	{0xD8, 0x07},
	{0xDC, 0x30},
	{0xDD, 0x52},
	{0xE0, 0x42},
	{0xE2, 0xE4},
	{0xE4, 0x00},
	{0xF0, 0xDC},
	{0xF1, 0x0E},
	{0xF2, 0x19},
	{0xF3, 0x0F},
	{0xF5, 0x95},
	{0xF6, 0x05},
	{0xF7, 0x00},
	{0xF8, 0x48},
	{0xFA, 0x25},
	{0x09, 0x01},
	{0xEF, 0x02},
	{0x2E, 0x0A},
	{0x33, 0x8A},
	{0x36, 0x00},
	{0x4F, 0x0A},
	{0x50, 0x0A},
	{0xCB, 0x9B},
	{0xD4, 0x00},
	{0xED, 0x01},
	{0xEF, 0x05},
	{0x0F, 0x00},
	{0x44, 0x00},
	{0xED, 0x01},
	{0xEF, 0x06},
	{0x00, 0x08},
	{0x02, 0x93},
	{0x08, 0x20},
	{0x09, 0xB0},
	{0x0A, 0x78},
	{0x0B, 0xD0},
	{0x0C, 0x60},
	{0x0D, 0x21},
	{0x0E, 0x30},
	{0x0F, 0x00},
	{0x10, 0xFA},
	{0x11, 0x00},
	{0x12, 0xFA},
	{0x17, 0x01},
	{0x18, 0x2C},
	{0x19, 0x01},
	{0x1A, 0x2C},
	{0x2B, 0x02},
	{0x2D, 0x04},
	{0x4A, 0x54},
	{0x4B, 0x97},
	{0x98, 0x40},
	{0x99, 0x06},
	{0x9A, 0x60},
	{0x9B, 0x09},
	{0x9C, 0xF0},
	{0x9D, 0x0A},
	{0x9F, 0x19},
	{0xA1, 0x04},
	{0xD7, 0x32},
	{0xD8, 0x02},
	{0xDA, 0x02},
	{0xDC, 0x02},
	{0xDE, 0x02},
	{0xE4, 0x02},
	{0xE6, 0x06},
	{0xE9, 0x10},
	{0xEB, 0x04},
	{0xED, 0x01},

	{PS5280_REG_END, 0x00},
};

static struct regval_list ps5280_init_regs_1920_1080_wdr_25fps[] = {
	{0xEF, 0x00},
	{0x0E, 0x45},
	{0x11, 0x00},
	{0x16, 0xBC},
	{0x36, 0x07},
	{0x37, 0x07},
	{0x38, 0x10},
	{0x63, 0x01},
	{0x64, 0x90},
	{0x67, 0x04},
	{0x68, 0xA0},
	{0x69, 0x10},
	{0x81, 0xC4},
	{0x85, 0x78},
	{0x9E, 0x40},
	{0xA3, 0x04},
	{0xA4, 0x68},
	{0xBE, 0x15},
	{0xDE, 0x03},
	{0xDF, 0x34},
	{0xE0, 0x10},
	{0xE1, 0x0A},
	{0xE2, 0x09},
	{0xE3, 0x34},
	{0xE4, 0x10},
	{0xE5, 0x0A},
	{0xE6, 0x09},
	{0xED, 0x01},
	{0xEF, 0x01},
	{0x04, 0x10},
	{0x05, 0x01},
	{0x0A, 0x05},
	{0x0B, 0x9F},
	{0x0C, 0x00},
	{0x0D, 0x04},
	{0x0E, 0x0F},
	{0x0F, 0xA0},
	{0x10, 0xC0},
	{0x11, 0x4B},
	{0x19, 0x40},
	{0x27, 0x11},
	{0x28, 0x94},
	{0x2E, 0xB4},
	{0x32, 0x1E},
	{0x33, 0x62},
	{0x37, 0x80},
	{0x39, 0x88},
	{0x3A, 0x1E},
	{0x3B, 0x6A},
	{0x3E, 0x11},
	{0x3F, 0xC8},
	{0x40, 0x1E},
	{0x42, 0xE8},
	{0x43, 0x62},
	{0x5D, 0x08},
	{0x5F, 0x32},
	{0x65, 0x1E},
	{0x66, 0x62},
	{0x78, 0x00},
	{0x8F, 0x05},
	{0x90, 0x00},
	{0x96, 0x80},
	{0xA3, 0x00},
	{0xA4, 0x12},
	{0xA5, 0x04},
	{0xA6, 0x38},
	{0xA7, 0x00},
	{0xA8, 0x08},
	{0xA9, 0x07},
	{0xAA, 0x80},
	{0xAB, 0x01},
	{0xB6, 0x0F},
	{0xB7, 0xA0},
	{0xB8, 0x00},
	{0xB9, 0x01},
	{0xBD, 0x02},
	{0xC9, 0x54},
	{0xCF, 0xBB},
	{0xD2, 0x22},
	{0xD3, 0x3E},
	{0xD4, 0xA4},
	{0xD6, 0x07},
	{0xD7, 0x0A},
	{0xD8, 0x07},
	{0xDC, 0x30},
	{0xDD, 0x52},
	{0xE0, 0x42},
	{0xE2, 0xE4},
	{0xE4, 0x00},
	{0xF0, 0xDC},
	{0xF1, 0x0E},
	{0xF2, 0x19},
	{0xF3, 0x0F},
	{0xF5, 0x95},
	{0xF6, 0x05},
	{0xF7, 0x00},
	{0xF8, 0x48},
	{0xFA, 0x25},
	{0x09, 0x01},
	{0xEF, 0x02},
	{0x2E, 0x0A},
	{0x33, 0x8A},
	{0x36, 0x00},
	{0x4F, 0x0A},
	{0x50, 0x0A},
	{0xCB, 0x9B},
	{0xD4, 0x00},
	{0xED, 0x01},
	{0xEF, 0x05},
	{0x0F, 0x00},
	{0x44, 0x00},
	{0xED, 0x01},
	{0xEF, 0x06},
	{0x00, 0x08},
	{0x02, 0x93},
	{0x08, 0x20},
	{0x09, 0xB0},
	{0x0A, 0x78},
	{0x0B, 0xD0},
	{0x0C, 0x60},
	{0x0D, 0x21},
	{0x0E, 0x30},
	{0x0F, 0x00},
	{0x10, 0xFA},
	{0x11, 0x00},
	{0x12, 0xFA},
	{0x17, 0x01},
	{0x18, 0x2C},
	{0x19, 0x01},
	{0x1A, 0x2C},
	{0x2B, 0x02},
	{0x2D, 0x04},
	{0x4A, 0x54},
	{0x4B, 0x97},
	{0x98, 0x40},
	{0x99, 0x06},
	{0x9A, 0x60},
	{0x9B, 0x09},
	{0x9C, 0xF0},
	{0x9D, 0x0A},
	{0x9F, 0x19},
	{0xA1, 0x04},
	{0xD7, 0x32},
	{0xD8, 0x02},
	{0xDA, 0x02},
	{0xDC, 0x02},
	{0xDE, 0x02},
	{0xE4, 0x02},
	{0xE6, 0x06},
	{0xE9, 0x10},
	{0xEB, 0x04},
	{0xED, 0x01},

	{PS5280_REG_END, 0x00},	/* END MARKER */
};

/*
 * the order of the ps5280_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting ps5280_win_sizes[] = {
	/* 1920*1080 @ nohdr mode */
	{
		.width		= 1920,
		.height		= 1080,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR12_1X12,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= ps5280_init_regs_1920_1080_linear_25fps,
	},
	/* 1920*1080 @ linear hdr mode */
	{
		.width		= 1920,
		.height		= 1080,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR12_1X12,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= ps5280_init_regs_1920_1080_wdr_25fps,
	}
};

/*
 * the part of driver was fixed.
 */

static struct regval_list ps5280_stream_on[] = {
	{PS5280_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list ps5280_stream_off[] = {
	{PS5280_REG_END, 0x00},	/* END MARKER */
};

int ps5280_read(struct tx_isp_subdev *sd, unsigned char reg, unsigned char *value)
{
	int ret;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg,
		},
		[1] = {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= value,
		}
	};

	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int ps5280_write(struct tx_isp_subdev *sd, unsigned char reg, unsigned char value)
{
	int ret;
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= buf,
	};

	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int ps5280_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != PS5280_REG_END) {
		if (vals->reg_num == PS5280_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ps5280_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
			if (vals->reg_num == PS5280_BANK_REG){
				val &= 0xe0;
				val |= (vals->value & 0x1f);
				ret = ps5280_write(sd, vals->reg_num, val);
				ret = ps5280_read(sd, vals->reg_num, &val);
			}
			pr_debug("ps5280_read_array ->> vals->reg_num:0x%02x, vals->reg_value:0x%02x\n",vals->reg_num, val);
		}
		vals++;
	}

	return 0;
}

static int ps5280_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != PS5280_REG_END) {
		if (vals->reg_num == PS5280_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ps5280_write(sd, vals->reg_num, vals->value);
			if (ret < 0){
				printk("ps5280_write error  %d\n" ,__LINE__);
				return ret;
			}
		}
		vals++;
	}

	return 0;
}

static int ps5280_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int ps5280_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;
	ret = ps5280_read(sd, 0x00, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0) {
		printk("err: ps5280 write error, ret= %d \n",ret);
		return ret;
	}
	if (v != PS5280_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = ps5280_read(sd, 0x01, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != PS5280_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int ps5280_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	unsigned int Cmd_OffNy = 0;
	unsigned int IntNep = 0;
	unsigned int IntNe = 0;
	unsigned int Const;

	Cmd_OffNy = ps5280_attr.total_height - value - 1;
	IntNep = NEPLS_LB + ((Cmd_OffNy*NEPLS_SCALE)>>8);
	IntNep = (IntNep > NEPLS_LB)?((IntNep < NEPLS_UB)?IntNep:NEPLS_UB):NEPLS_LB;
	if(sensor_raw_mode == SENSOR_RAW_MODE_LINEAR)
		Const = NE_NEP_CONST_LINEAR;
	else if(sensor_raw_mode == SENSOR_RAW_MODE_NATIVE_WDR)
		Const = NE_NEP_CONST_WDR;
	else
		printk("Now we do not support this sensor raw mode!!!\n");
	IntNe = Const - IntNep;

	ret = ps5280_write(sd, 0xef, 0x01);
	ret += ps5280_write(sd, 0x0c, (unsigned char)((Cmd_OffNy & 0xff00) >> 8));
	ret += ps5280_write(sd, 0x0d, (unsigned char)(Cmd_OffNy & 0xff));
	/*Exp Pixel Control*/
	ret += ps5280_write(sd, 0x0e, (unsigned char)((IntNe & 0xff00) >> 8));
	ret += ps5280_write(sd, 0x0f, (unsigned char)(IntNe & 0xff));
	ret += ps5280_write(sd, 0x5e, (unsigned char)((IntNep & 0xff00) >> 8));
	ret += ps5280_write(sd, 0x6f, (unsigned char)(IntNep & 0xff));
	ret += ps5280_write(sd, 0x09, 0x01);
	if (ret < 0)
		return ret;

	return 0;
}

static int ps5280_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	unsigned int gain = value;
	static unsigned int mode = 0;

	if (gain > AG_HS_NODE){
		mode = 0;
	} else if(gain < AG_LS_NODE){
		mode = 1;
	}
	if(mode == 0)	gain -= 32;		// For 4x ratio
	ret = ps5280_write(sd, 0xef, 0x01);
	ret += ps5280_write(sd, 0x18, (unsigned char)(mode & 0x01));
	ret += ps5280_write(sd, 0x83, (unsigned char)(gain & 0xff));
	ret += ps5280_write(sd, 0x09, 0x01);
	if (ret < 0)
		return ret;

	return 0;
}

static int ps5280_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int ps5280_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int ps5280_init(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = 0;

	if(!enable)
		return ISP_SUCCESS;

	switch (sensor_raw_mode) {
	case SENSOR_RAW_MODE_LINEAR:
		wsize = &ps5280_win_sizes[0];
		break;
	case SENSOR_RAW_MODE_NATIVE_WDR:
		wsize = &ps5280_win_sizes[1];
		break;
	default:
		printk("Now we do not support this sensor raw mode!!!\n");
	}
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	ret = ps5280_write_array(sd, wsize->regs);
	if (ret)
		return ret;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int ps5280_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		ret = ps5280_write_array(sd, ps5280_stream_on);
		pr_debug("ps5280 stream on\n");
	} else {
		ret = ps5280_write_array(sd, ps5280_stream_off);
		pr_debug("ps5280 stream off\n");
	}

	return ret;
}

static int ps5280_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int pclk = PS5280_SUPPORT_PCLK;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned int Cmd_Lpf = 0;
	unsigned int Cur_OffNy = 0;
	unsigned int Cur_ExpLine = 0;
	unsigned char tmp;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	/* the format of fps is 16/16. for example 25 << 16 | 2, the value is 25/2 fps. */
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)){
		printk("warn: fps(%d) no in range\n", fps);
		return -1;
	}
	ret = ps5280_write(sd, 0xef, 0x01);
	if(ret < 0)
		return -1;
	ret = ps5280_read(sd, 0x27, &tmp);
	hts = tmp;
	ret += ps5280_read(sd, 0x28, &tmp);
	if(ret < 0)
		return -1;
	/*hdr mode hts = line time / 2*/
	if(sensor_raw_mode==SENSOR_RAW_MODE_LINEAR)
		hts = (((hts & 0x1f) << 8) | tmp);
	else if(sensor_raw_mode==SENSOR_RAW_MODE_NATIVE_WDR)
		hts = (((hts & 0x1f) << 8) | tmp) >> 1;
	else
		printk("Now ps5280 Do not support this sensor raw mode.\n");

	printk("#### hts=0x%x=%d\n",hts,hts);
	vts = (pclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16));
	printk("#### vts=0x%x=%d\n",vts,vts);
	Cmd_Lpf = vts -1;
	ret = ps5280_write(sd, 0xef, 0x01);
	ret += ps5280_write(sd, 0x0b, (unsigned char)(Cmd_Lpf & 0xff));
	ret += ps5280_write(sd, 0x0a, (unsigned char)(Cmd_Lpf >> 8));
	ret += ps5280_write(sd, 0x09, 0x01);
	if(ret < 0){
		printk("err: ps5280_write err\n");
		return ret;
	}
	ret = ps5280_read(sd, 0x0c, &tmp);
	Cur_OffNy = tmp;
	ret += ps5280_read(sd, 0x0d, &tmp);
	if(ret < 0)
		return -1;
	Cur_OffNy = (((Cur_OffNy & 0xff) << 8) | tmp);
	Cur_ExpLine = ps5280_attr.total_height - Cur_OffNy;

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 4;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	ret = ps5280_set_integration_time(sd, Cur_ExpLine);
	if(ret < 0)
		return -1;

	return ret;
}

static int ps5280_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = ISP_SUCCESS;

	if(value == TX_ISP_SENSOR_FULL_RES_MAX_FPS){
		if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR)
			wsize = &ps5280_win_sizes[0];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_NATIVE_WDR)
			wsize = &ps5280_win_sizes[1];
		else
			printk("Now ps5280 Do not support this sensor raw mode.\n");
	}else if(value == TX_ISP_SENSOR_PREVIEW_RES_MAX_FPS){
		if (sensor_raw_mode == SENSOR_RAW_MODE_LINEAR)
			wsize = &ps5280_win_sizes[0];
		else if (sensor_raw_mode == SENSOR_RAW_MODE_NATIVE_WDR)
			wsize = &ps5280_win_sizes[1];
		else
			printk("Now ps5280 Do not support this sensor raw mode.\n");
	}

	if(wsize){
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = V4L2_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}

	return ret;
}

static int ps5280_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;
	unsigned char val = 0;

	ret = ps5280_write(sd, 0xef, 0x01);
	ret += ps5280_read(sd, 0x1d, &val);
	if (enable)
		val = val | 0x80;
	else
		val = val & 0x7F;

	ret += ps5280_write(sd, 0xef, 0x01);
	ret += ps5280_write(sd, 0x1d, val);
	ret += ps5280_write(sd, 0x09, 0x01);
	sensor->video.mbus_change = 0;

	if(!ret)
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int ps5280_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	/*if(pwdn_gpio != -1){
	  ret = private_gpio_request(pwdn_gpio,"ps5280_pwdn");
	  if(!ret){
	  private_gpio_direction_output(pwdn_gpio, 1);
	  private_msleep(50);
	  private_gpio_direction_output(pwdn_gpio, 0);
	  private_msleep(10);
	  }else{
	  printk("gpio requrest fail %d\n",pwdn_gpio);
	  }
	  }*/
	if (reset_gpio != -1) {
		ret = private_gpio_request(reset_gpio,"ps5280_reset");
		if (!ret) {
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(20);
		} else {
			printk("gpio requrest fail %d\n",reset_gpio);
		}
	}

	ret = ps5280_detect(sd, &ident);
	if (ret) {
		printk("chip found @ 0x%x (%s) is not an ps5280 chip.\n",
		       client->addr, client->adapter->name);
		return ret;
	}
	printk("ov2735 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	if(chip){
		memcpy(chip->name, "ps5280", sizeof("ps5280"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}

static int ps5280_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if(IS_ERR_OR_NULL(sd)){
		printk("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if(arg)
			ret = ps5280_set_integration_time(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if(arg)
			ret = ps5280_set_analog_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = ps5280_set_digital_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = ps5280_get_black_pedestal(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
			ret = ps5280_set_mode(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		ret = ps5280_write_array(sd, ps5280_stream_off);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = ps5280_write_array(sd, ps5280_stream_on);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
		ret = ps5280_set_fps(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if(arg)
			ret = ps5280_set_vflip(sd, *(int*)arg);
		break;
	default:
		break;;
	}
	return 0;
}

static int ps5280_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
		return -EINVAL;
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = ps5280_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int ps5280_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
		return -EINVAL;
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ps5280_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops ps5280_core_ops = {
	.g_chip_ident = ps5280_g_chip_ident,
	.reset = ps5280_reset,
	.init = ps5280_init,
	.g_register = ps5280_g_register,
	.s_register = ps5280_s_register,
};

static struct tx_isp_subdev_video_ops ps5280_video_ops = {
	.s_stream = ps5280_s_stream,
};

static struct tx_isp_subdev_sensor_ops	ps5280_sensor_ops = {
	.ioctl	= ps5280_sensor_ops_ioctl,
};
static struct tx_isp_subdev_ops ps5280_ops = {
	.core = &ps5280_core_ops,
	.video = &ps5280_video_ops,
	.sensor = &ps5280_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "ps5280",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};


static int ps5280_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_win_setting *wsize = &ps5280_win_sizes[0];
	int ret;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		printk("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));
	/* request mclk of sensor */
	sensor->mclk = clk_get(NULL, "cgu_cim");
	if (IS_ERR(sensor->mclk)) {
		printk("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
#if 0
	rate = clk_get_rate(clk_get_parent(sensor->mclk));
	if (((rate / 1000) % 27000) != 0) {
		struct clk *epll;
		epll = clk_get(NULL,"epll");
		if (IS_ERR(epll)) {
			pr_err("get epll failed\n");
		} else {
			rate = clk_get_rate(epll);
			if (((rate / 1000) % 27000) != 0) {
				clk_set_rate(epll,864000000);
			}
			ret = clk_set_parent(sensor->mclk, epll);
			if (ret < 0)
				pr_err("set mclk parent as epll err\n");
		}
	}

	clk_set_rate(sensor->mclk, 27000000);
	clk_enable(sensor->mclk);
	printk("mclk=%lu\n", clk_get_rate(sensor->mclk));
#endif

	clk_set_rate(sensor->mclk, 24000000);
	clk_enable(sensor->mclk);

	ret = set_sensor_gpio_function(sensor_gpio_func);
	if (ret < 0)
		goto err_set_sensor_gpio;
	ps5280_attr.dvp.gpio = sensor_gpio_func;
#if 0
	switch(sensor_gpio_func){
	case DVP_PA_LOW_10BIT:
	case DVP_PA_HIGH_10BIT:
		mbus = ps5280_mbus_code[0];
		break;
	case DVP_PA_12BIT:
		mbus = ps5280_mbus_code[1];
		break;
	default:
		goto err_set_sensor_gpio;
	}

	for(i = 0; i < ARRAY_SIZE(ps5280_win_sizes); i++)
		ps5280_win_sizes[i].mbus_code = mbus;

#endif
	/*
	  convert sensor-gain into isp-gain,
	*/
	ps5280_attr.max_again = 458752;
	ps5280_attr.max_dgain = 0; //ps5280_attr.max_dgain;
	sd = &sensor->sd;
	video = &sensor->video;
	switch(sensor_raw_mode){
	case SENSOR_RAW_MODE_LINEAR:
		wsize = &ps5280_win_sizes[0];
		break;
	case SENSOR_RAW_MODE_NATIVE_WDR:
		wsize = &ps5280_win_sizes[1];
		break;
	default:
		printk("Now ps5280 Do not support this sensor raw mode.\n");
		break;
	}
	sensor->video.attr = &ps5280_attr;
	sensor->video.mbus_change = 0;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &ps5280_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->ps5280\n");

	return 0;
err_set_sensor_gpio:
	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
err_get_mclk:
	kfree(sensor);

	return -1;
}

static int ps5280_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if (reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if (pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id ps5280_id[] = {
	{ "ps5280", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ps5280_id);

static struct i2c_driver ps5280_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ps5280",
	},
	.probe		= ps5280_probe,
	.remove		= ps5280_remove,
	.id_table	= ps5280_id,
};

static __init int init_ps5280(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if (ret) {
		printk("Failed to init ps5280 dirver.\n");
		return -1;
	}

	return private_i2c_add_driver(&ps5280_driver);
}

static __exit void exit_ps5280(void)
{
	private_i2c_del_driver(&ps5280_driver);
}

module_init(init_ps5280);
module_exit(exit_ps5280);

MODULE_DESCRIPTION("A low-level driver for Primesensor ps5280 sensors");
MODULE_LICENSE("GPL");
