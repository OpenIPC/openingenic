/*
 * jxf51.c
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

#define JXF51_CHIP_ID_H	        (0x0f)
#define JXF51_CHIP_ID_L	        (0x51)
#define JXF51_REG_END		0xff
#define JXF51_REG_DELAY		0xfe
#define JXF51_SUPPORT_30FPS_SCLK_MIPI (72000000)
#define JXF51_SUPPORT_30FPS_SCLK_WDR (86348160)
#define SENSOR_OUTPUT_MAX_FPS_WDR 20
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION	"H20210301a"

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int sensor_en = GPIO_PB(21);
module_param(sensor_en, int, S_IRUGO);
MODULE_PARM_DESC(sensor_en, "Sensor EN NUM");

static int pwdn_gpio = GPIO_PA(16);
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func = DVP_PA_LOW_10BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

static int data_interface = TX_SENSOR_DATA_INTERFACE_MIPI;
module_param(data_interface, int, S_IRUGO);
MODULE_PARM_DESC(data_interface, "Sensor Date interface");

static int data_type = TX_SENSOR_DATA_TYPE_LINEAR;
module_param(data_type, int, S_IRUGO);
MODULE_PARM_DESC(data_type, "Sensor Date Type");

static int wdr_bufsize = 2596800;//cache lines corrponding on VPB1
module_param(wdr_bufsize, int, S_IRUGO);
MODULE_PARM_DESC(wdr_bufsize, "Wdr Buf Size");

static unsigned char reg_2f = 0x44;
static unsigned char reg_0c = 0x00;
static unsigned char reg_82 = 0x21;

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

struct again_lut jxf51_again_lut[] = {
	{0x0, 0 },
	{0x1, 5731 },
	{0x2, 11136},
	{0x3, 16248},
	{0x4, 21097},
	{0x5, 25710},
	{0x6, 30109},
	{0x7, 34312},
	{0x8, 38336},
	{0x9, 42195},
	{0xa, 45904},
	{0xb, 49472},
	{0xc, 52910},
	{0xd, 56228},
	{0xe, 59433},
	{0xf, 62534},
	{0x10, 65536},
	{0x11, 71267},
	{0x12, 76672},
	{0x13, 81784},
	{0x14, 86633},
	{0x15, 91246},
	{0x16, 95645},
	{0x17, 99848},
	{0x18, 103872},
	{0x19, 107731},
	{0x1a, 111440},
	{0x1b, 115008},
	{0x1c, 118446},
	{0x1d, 121764},
	{0x1e, 124969},
	{0x1f, 128070},
	{0x20, 131072},
	{0x21, 136803},
	{0x22, 142208},
	{0x23, 147320},
	{0x24, 152169},
	{0x25, 156782},
	{0x26, 161181},
	{0x27, 165384},
	{0x28, 169408},
	{0x29, 173267},
	{0x2a, 176976},
	{0x2b, 180544},
	{0x2c, 183982},
	{0x2d, 187300},
	{0x2e, 190505},
	{0x2f, 193606},
	{0x30, 196608},
	{0x31, 202339},
	{0x32, 207744},
	{0x33, 212856},
	{0x34, 217705},
	{0x35, 222318},
	{0x36, 226717},
	{0x37, 230920},
	{0x38, 234944},
	{0x39, 238803},
	{0x3a, 242512},
	{0x3b, 246080},
	{0x3c, 249518},
	{0x3d, 252836},
	{0x3e, 256041},
	{0x3f, 259142},
};

struct tx_isp_sensor_attribute jxf51_attr;

unsigned int jxf51_alloc_integration_time_short(unsigned int it, unsigned char shift, unsigned int *sensor_it)
{
	unsigned int expo = it >> shift;
	unsigned int isp_it = it;

	if (expo % 2 == 0)
		expo = expo - 1;
	if(expo < jxf51_attr.min_integration_time_short)
		expo = 3;
	isp_it = expo << shift;
	expo = (expo - 1) / 2;
	if(expo < 0)
		expo = 0;
	*sensor_it = expo;

	return isp_it;
}

unsigned int jxf51_alloc_again_short(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = jxf51_again_lut;
	while(lut->gain <= jxf51_attr.max_again_short) {
		if(isp_gain == 0) {
			*sensor_again = 0;
			return 0;
		}
		else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else{
			if((lut->gain == jxf51_attr.max_again_short) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int jxf51_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = jxf51_again_lut;
	while(lut->gain <= jxf51_attr.max_again) {
		if(isp_gain == 0) {
			*sensor_again = 0;
			return 0;
		}
		else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else{
			if((lut->gain == jxf51_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}


unsigned int jxf51_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_mipi_bus jxf51_mipi_dol={
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 800,
	.lans = 2,
	.settle_time_apative_en = 1,
	.image_twidth = 1536,
	.image_theight = 1536,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = RAW10,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_WDR_2_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_VC_MODE,
};

struct tx_isp_mipi_bus jxf51_mipi={
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 800,
	.lans = 2,
	.settle_time_apative_en = 1,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 1,
	.mipi_sc.mipi_hcomp_en = 0,
	.mipi_sc.line_sync_mode = 0,
	.image_twidth = 1536,
	.image_theight = 1536,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.work_start_flag = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = RAW10,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
	//.clk = 648,
	//.lans = 2,
};


struct tx_isp_dvp_bus jxf51_dvp = {
	.mode = SENSOR_DVP_HREF_MODE,
	.blanking = {
		.vblanking = 0,
		.hblanking = 0,
	},
};
struct tx_isp_sensor_attribute jxf51_attr = {
	.name = "jxf51",
	.chip_id = 0xf51,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x40,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
	},
	.max_again = 259142,
	.max_again_short = 259142,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_short = 1,
	.max_integration_time_short = 505,
	.min_integration_time_native = 2,
	.max_integration_time_native = 2000 - 4,
	.integration_time_limit = 2000 - 4,
	.total_width = 2400,
	.total_height = 2000,
	.max_integration_time = 2000 - 4,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = jxf51_alloc_again,
	.sensor_ctrl.alloc_again_short = jxf51_alloc_again_short,
	.sensor_ctrl.alloc_dgain = jxf51_alloc_dgain,
	.sensor_ctrl.alloc_integration_time_short = jxf51_alloc_integration_time_short,
	//	void priv; /* point to struct tx_isp_sensor_board_info */
};


static struct regval_list jxf51_init_regs_1536_1536_30fps_mipi[] = {
    {0x12,0x40},
    {0x48,0x8B},
    {0x48,0x0B},
    {0x0E,0x11},
    {0x0F,0x04},
    {0x10,0x3C},
    {0x11,0x80},
    {0x0D,0x50},
    {0x57,0x60},
    {0x58,0x1B},
    {0x5F,0x41},
    {0x60,0x20},
    {0x61,0x08},
    {0x07,0x08},
    {0x20,0x58},
    {0x21,0x02},
    {0x22,0xD0},
    {0x23,0x07},
    {0x24,0x80},
    {0x25,0x00},
    {0x26,0x61},
    {0x27,0x4F},
    {0x28,0x15},
    {0x29,0x02},
    {0x2A,0x48},
    {0x2B,0x12},
    {0x2C,0x00},
    {0x2D,0x00},
    {0x2E,0x86},
    {0x2F,0x44},
    {0x41,0x04},
    {0x42,0x03},
    {0x46,0x18},
    {0x47,0x42},
    {0x80,0x01},
    {0xAF,0x12},
    {0xBD,0x00},
    {0xBE,0x06},
    {0xAB,0x00},
    {0x1D,0x00},
    {0x1E,0x04},
    {0x6C,0x40},
    {0x70,0xD1},
    {0x71,0x8B},
    {0x72,0x6D},
    {0x73,0x49},
    {0x75,0x1B},
    {0x74,0x12},
    {0x89,0x11},
    {0x0C,0x00},
    {0x6B,0x00},
    {0x86,0x40},
    {0x6E,0x2C},
    {0x78,0x14},
    {0x76,0x67},
    {0x2F,0x44},
    {0x31,0x10},
    {0x32,0x25},
    {0x33,0x5C},
    {0x34,0x23},
    {0x35,0x2B},
    {0x3A,0xA1},
    {0x3B,0xA0},
    {0x3C,0x2B},
    {0x3D,0x00},
    {0x3E,0x00},
    {0x3F,0x8E},
    {0x40,0x93},
    {0x56,0x12},
    {0x59,0x40},
    {0x5A,0x10},
    {0x85,0x3A},
    {0xBF,0x01},
    {0x4D,0x08},
    {0x53,0x01},
    {0xBF,0x00},
    {0x9C,0xA1},
    {0x62,0x21},
    {0x64,0xE0},
    {0x65,0x33},
    {0x66,0x13},
    {0x67,0x57},
    {0x68,0x00},
    {0x69,0xFC},
    {0x6A,0x45},
    {0x7A,0x40},
    {0x8F,0x10},
    {0xBF,0x01},
    {0x45,0x07},
    {0x46,0x3C},
    {0x47,0x88},
    {0x48,0xFC},
    {0x49,0x21},
    {0x4B,0x42},
    {0xBF,0x00},
    {0x97,0xA2},
    {0x13,0x81},
    {0x96,0x04},
    {0x4A,0x05},
    {0x7E,0xC9},
    {0xA7,0x04},
    {0x50,0x02},
    {0x49,0x10},
    {0x7B,0x4A},
    {0x7C,0x0A},
    {0x7F,0x57},
    {0x90,0x00},
    {0x8C,0xFF},
    {0x8D,0xC7},
    {0x8E,0x80},
    {0x8B,0x01},
    {0xBF,0x01},
    {0x4E,0x11},
    {0xBF,0x00},
    {0x82,0x00},
    {0x19,0x20},
    {0x12,0x00},
	{JXF51_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list jxf51_init_regs_1536_1536_20fps_mipi_wdr[] = {
    {0x12, 0x48},
    {0x48, 0x8B},
    {0x48, 0x0B},
    {0x0E, 0x11},
    {0x0F, 0x04},
    {0x10, 0x48},
    {0x11, 0x80},
    {0x0D, 0x50},
    {0x57, 0x60},
    {0x58, 0x1B},
    {0x5F, 0x41},
    {0x60, 0x30},
    {0x61, 0x08},
    {0x07, 0x0B},
    {0x20, 0xAC},
    {0x21, 0x02},
    {0x22, 0x54},
    {0x23, 0x0C},
    {0x24, 0x80},
    {0x25, 0x00},
    {0x26, 0x61},
    {0x27, 0xF7},
    {0x28, 0x29},
    {0x29, 0x01},
    {0x2A, 0xF0},
    {0x2B, 0x11},
    {0x2C, 0x00},
    {0x2D, 0x00},
    {0x2E, 0x86},
    {0x2F, 0x44},
    {0x41, 0x04},
    {0x42, 0x03},
    {0x46, 0x1C},
    {0x47, 0x42},
    {0x80, 0x01},
    {0xAF, 0x12},
    {0xBD, 0x00},
    {0xBE, 0x06},
    {0xAB, 0x00},
    {0x1D, 0x00},
    {0x1E, 0x04},
    {0x6C, 0x40},
    {0x70, 0xD9},
    {0x71, 0x90},
    {0x72, 0x8D},
    {0x73, 0x69},
    {0x75, 0x9B},
    {0x74, 0x12},
    {0x89, 0x17},
    {0x0C, 0x00},
    {0x6B, 0x00},
    {0x86, 0x40},
    {0x6E, 0x2C},
    {0x78, 0x14},
    {0x76, 0x67},
    {0x2F, 0x44},
    {0x31, 0x14},
    {0x32, 0x2C},
    {0x33, 0x60},
    {0x34, 0x5C},
    {0x35, 0x5C},
    {0x3A, 0xA1},
    {0x3B, 0xE4},
    {0x3C, 0x23},
    {0x3D, 0x00},
    {0x3E, 0x00},
    {0x3F, 0xAC},
    {0x40, 0x76},
    {0x56, 0x12},
    {0x59, 0x7C},
    {0x5A, 0x10},
    {0x85, 0x39},
    {0xBF, 0x01},
    {0x4D, 0x08},
    {0x53, 0x01},
    {0xBF, 0x00},
    {0x9C, 0xA1},
    {0x62, 0x21},
    {0x64, 0xE0},
    {0x65, 0x33},
    {0x66, 0x13},
    {0x67, 0x57},
    {0x68, 0x00},
    {0x69, 0xFC},
    {0x6A, 0x45},
    {0x7A, 0x40},
    {0x8F, 0x10},
    {0xBF, 0x01},
    {0x45, 0x07},
    {0x46, 0x3C},
    {0x47, 0x88},
    {0x48, 0xFC},
    {0x49, 0x21},
    {0x4B, 0x42},
    {0xBF, 0x00},
    {0x97, 0xA2},
    {0x13, 0x81},
    {0x96, 0x04},
    {0x4A, 0x05},
    {0x7E, 0xC9},
    {0xA7, 0x04},
    {0x50, 0x02},
    {0x49, 0x10},
    {0x7B, 0x4A},
    {0x7C, 0x0A},
    {0x7F, 0x57},
    {0x90, 0x00},
    {0x8C, 0xFF},
    {0x8D, 0xC7},
    {0x8E, 0x80},
    {0x8B, 0x01},
    {0xBF, 0x01},
    {0x4E, 0x11},
    {0xBF, 0x00},
    {0x82, 0x00},
    {0x19, 0x20},
    {0x1B, 0x4F},
    {0x06, 0x23},
    {0x03, 0xFF},
    {0x04, 0xFF},
    {0x12, 0x08},

    {JXF51_REG_END, 0x00},	/* END MARKER */
};
/*
 * the order of the jxf51_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting jxf51_win_sizes[] = {
	{
		.width		= 1536,
		.height		= 1536,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= jxf51_init_regs_1536_1536_30fps_mipi,
	},
	{
		.width		= 1536,
		.height		= 1536,
		.fps		= 20 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= jxf51_init_regs_1536_1536_20fps_mipi_wdr,
	},
};
static struct tx_isp_sensor_win_setting *wsize = &jxf51_win_sizes[0];


/*
 * the part of driver was fixed.
 */

static struct regval_list jxf51_stream_on_dvp[] = {
	{0x12, 0x20},
	{JXF51_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list jxf51_stream_off_dvp[] = {
	{0x12, 0x40},
	{JXF51_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list jxf51_stream_on_mipi[] = {
	{JXF51_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list jxf51_stream_off_mipi[] = {
	{JXF51_REG_END, 0x00},	/* END MARKER */
};



int jxf51_read(struct tx_isp_subdev *sd, unsigned char reg,
	       unsigned char *value)
{
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
	int ret;
	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}


int jxf51_write(struct tx_isp_subdev *sd, unsigned char reg,
		unsigned char value)
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

static int jxf51_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;

	while (vals->reg_num != JXF51_REG_END) {
		if (vals->reg_num == JXF51_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = jxf51_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int jxf51_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != JXF51_REG_END) {
		if (vals->reg_num == JXF51_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = jxf51_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int jxf51_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int jxf51_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;

	ret = jxf51_read(sd, 0x0a, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != JXF51_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = jxf51_read(sd, 0x0b, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;

	if (v != JXF51_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int jxf51_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	ret = jxf51_write(sd,  0x01, (unsigned char)(value & 0xff));
	ret += jxf51_write(sd, 0x02, (unsigned char)((value >> 8) & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}

static int jxf51_set_integration_time_short(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = jxf51_write(sd, 0x05, (unsigned char)(value & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}

static int jxf51_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret += jxf51_write(sd, 0x00, (unsigned char)(value & 0x7f));

	if (ret < 0)
		return ret;

	return 0;
}

static int jxf51_set_analog_gain_short(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int jxf51_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int jxf51_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}


static int jxf51_init(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if(!enable)
		return ISP_SUCCESS;

	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	ret = jxf51_write_array(sd, wsize->regs);

	ret += jxf51_read(sd, 0x2f, &reg_2f);
	ret += jxf51_read(sd, 0x0c, &reg_0c);
	ret += jxf51_read(sd, 0x82, &reg_82);

	if (ret)
		return ret;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int jxf51_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = jxf51_write_array(sd, jxf51_stream_on_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = jxf51_write_array(sd, jxf51_stream_on_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		pr_debug("jxf51 stream on\n");

	}
	else {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = jxf51_write_array(sd, jxf51_stream_off_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = jxf51_write_array(sd, jxf51_stream_off_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		pr_debug("jxf51 stream off\n");
	}
	return ret;
}

static int jxf51_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned char val = 0;
	unsigned int newformat = 0; //the format is 24.8
	unsigned int max_fps = SENSOR_OUTPUT_MAX_FPS;

	switch (data_type) {
	case TX_SENSOR_DATA_TYPE_LINEAR:
		sclk = JXF51_SUPPORT_30FPS_SCLK_MIPI;
                max_fps = SENSOR_OUTPUT_MAX_FPS;
		break;
	case TX_SENSOR_DATA_TYPE_WDR_DOL:
		sclk = JXF51_SUPPORT_30FPS_SCLK_WDR;
                max_fps = SENSOR_OUTPUT_MAX_FPS_WDR;
		break;
	default:
		ret = -1;
		ISP_ERROR("Now we do not support this data type!!!\n");
	}

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%d) no in range\n", fps);
		return -1;
	}
	val = 0;
	ret += jxf51_read(sd, 0x21, &val);
	hts = val<<8;
	val = 0;
	ret += jxf51_read(sd, 0x20, &val);
	hts |= val;
        hts *= 2;
	if (0 != ret) {
		ISP_ERROR("err: jxf51 read err\n");
		return ret;
	}

	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);

	jxf51_write(sd, 0xc0, 0x22);
	jxf51_write(sd, 0xc1, (unsigned char)(vts & 0xff));
	jxf51_write(sd, 0xc2, 0x23);
	jxf51_write(sd, 0xc3, (unsigned char)(vts >> 8));
	ret = jxf51_read(sd, 0x1f, &val);
	pr_debug("before register 0x1f value : 0x%02x\n", val);
	if(ret < 0)
		return -1;
	val |= (1 << 7); //set bit[7],  register group write function,  auto clean
	jxf51_write(sd, 0x1f, val);
	pr_debug("after register 0x1f value : 0x%02x\n", val);

	if (0 != ret) {
		ISP_ERROR("err: jxf51_write err\n");
		return ret;
	}
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 4;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static unsigned char val0,val1,val2;
static int jxf51_set_wdr(struct tx_isp_subdev *sd, int wdr_en)
{
	int ret = 0;

	ret = jxf51_write(sd, 0x12, 0x80);
	private_msleep(5);

	ret = jxf51_write_array(sd, wsize->regs);
	ret = jxf51_write(sd, 0x00, val0);
	ret = jxf51_write(sd, 0x01, val1);
	ret = jxf51_write(sd, 0x02, val2);
	ret = jxf51_write_array(sd, jxf51_stream_on_mipi);
	ret = jxf51_write(sd, 0x00, 0x00);

	return 0;
}

static int jxf51_set_wdr_stop(struct tx_isp_subdev *sd, int wdr_en)
{
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);
	int ret = 0;
	ret = jxf51_read(sd, 0x00, &val0);
	ret = jxf51_read(sd, 0x01, &val1);
	ret = jxf51_read(sd, 0x02, &val2);

	ret = jxf51_write(sd, 0x12, 0x40);
	if(wdr_en == 1){
		memcpy((void*)(&(jxf51_attr.mipi)),(void*)(&jxf51_mipi_dol),sizeof(jxf51_mipi_dol));
		data_type = TX_SENSOR_DATA_TYPE_WDR_DOL;
		wsize = &jxf51_win_sizes[1];
		jxf51_attr.data_type = data_type;
		jxf51_attr.wdr_cache = wdr_bufsize;
		jxf51_attr.one_line_expr_in_us = 28;

		jxf51_attr.wdr_cache = wdr_bufsize;
		jxf51_attr.max_integration_time_native = 1578*2 - 0x23*2 - 6;
		jxf51_attr.integration_time_limit = 1578*2 - 0x23*2 - 6;
		jxf51_attr.total_width = 2736;
		jxf51_attr.total_height = 1578*2;
		jxf51_attr.max_integration_time = 1578*2 - 0x23*2 -6;

		sensor->video.attr = &jxf51_attr;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	} else if (wdr_en == 0){
		memcpy((void*)(&(jxf51_attr.mipi)),(void*)(&jxf51_mipi),sizeof(jxf51_mipi));

		data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		jxf51_attr.data_type = data_type;
		wsize = &jxf51_win_sizes[0];

		jxf51_attr.one_line_expr_in_us = 30;
		jxf51_attr.data_type = data_type;
		jxf51_attr.max_integration_time_native = 2000 - 4;
		jxf51_attr.integration_time_limit = 2000 - 4;
		jxf51_attr.total_width = 2400;
		jxf51_attr.total_height = 2000;
		jxf51_attr.max_integration_time = 2000 - 4;

		sensor->video.attr = &jxf51_attr;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	} else {
		ISP_ERROR("Can not support this data type!!!");
		return -1;
	}

	return 0;
}


static int jxf51_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

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

static int jxf51_g_chip_ident(struct tx_isp_subdev *sd,
			      struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	if(reset_gpio != -1){
		ret = private_gpio_request(reset_gpio,"jxf51_reset");
		if(!ret){
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(50);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(35);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(35);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",reset_gpio);
		}
	}
	if(pwdn_gpio != -1){
		ret = private_gpio_request(pwdn_gpio,"jxf51_pwdn");
		if(!ret){
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(150);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	if(sensor_en != -1){
		ret = private_gpio_request(sensor_en,"jxf51_sensor");
		if(!ret){
			private_gpio_direction_output(sensor_en, 0);
			private_msleep(150);
			private_gpio_direction_output(sensor_en, 1);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",sensor_en);
		}
	}

	ret = jxf51_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an jxf51 chip.\n",
			  client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("jxf51 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	ISP_WARNING("sensor driver version %s\n",SENSOR_VERSION);
	if(chip){
		memcpy(chip->name, "jxf51", sizeof("jxf51"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}

static int jxf51_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if(arg)
			ret = jxf51_set_integration_time(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME_SHORT:
		if(arg)
			ret = jxf51_set_integration_time_short(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if(arg)
			ret = jxf51_set_analog_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN_SHORT:
		if(arg)
			ret = jxf51_set_analog_gain_short(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = jxf51_set_digital_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = jxf51_get_black_pedestal(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
			ret = jxf51_set_mode(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = jxf51_write_array(sd, jxf51_stream_off_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = jxf51_write_array(sd, jxf51_stream_off_mipi);

		} else {
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = jxf51_write_array(sd, jxf51_stream_on_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = jxf51_write_array(sd, jxf51_stream_on_mipi);

		} else {
			ISP_ERROR("Don't support this Sensor Data interface\n");
			ret = -1;
		}
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
			ret = jxf51_set_fps(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_WDR:
		if(arg)
			ret = jxf51_set_wdr(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_WDR_STOP:
		if(arg)
			ret = jxf51_set_wdr_stop(sd, *(int*)arg);
		break;
	default:
		break;;
	}

	return ret;
}


static int jxf51_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if(len && strncmp(sd->chip.name, reg->name, len)){
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = jxf51_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;
	return ret;
}

static int jxf51_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if(len && strncmp(sd->chip.name, reg->name, len)){
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	jxf51_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}


static struct tx_isp_subdev_core_ops jxf51_core_ops = {
	.g_chip_ident = jxf51_g_chip_ident,
	.reset = jxf51_reset,
	.init = jxf51_init,
	/*.ioctl = jxf51_ops_ioctl,*/
	.g_register = jxf51_g_register,
	.s_register = jxf51_s_register,
};

static struct tx_isp_subdev_video_ops jxf51_video_ops = {
	.s_stream = jxf51_s_stream,
};

static struct tx_isp_subdev_sensor_ops	jxf51_sensor_ops = {
	.ioctl	= jxf51_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops jxf51_ops = {
	.core = &jxf51_core_ops,
	.video = &jxf51_video_ops,
	.sensor = &jxf51_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "jxf51",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int jxf51_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
//	int ret;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if(!sensor){
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));
	sensor->mclk = clk_get(NULL, "cgu_cim");
	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_enable(sensor->mclk);


        if (data_type == TX_SENSOR_DATA_TYPE_LINEAR) {
		wsize = &jxf51_win_sizes[0];
		memcpy((void*)(&(jxf51_attr.mipi)),(void*)(&jxf51_mipi),sizeof(jxf51_mipi));
		jxf51_attr.min_integration_time = 2;
                jxf51_attr.max_integration_time_native = 2000 - 4;
		jxf51_attr.integration_time_limit = 2000 - 4;
		jxf51_attr.total_width = 2400;
		jxf51_attr.total_height = 2000;
		jxf51_attr.max_integration_time = 2000 - 4;
		//jxf51_attr.mipi.clk = 648;
		jxf51_attr.mipi.settle_time_apative_en = 1;
	} else if (data_type == TX_SENSOR_DATA_TYPE_WDR_DOL) {
		wsize = &jxf51_win_sizes[1];
		memcpy((void*)(&(jxf51_attr.mipi)),(void*)(&jxf51_mipi_dol),sizeof(jxf51_mipi_dol));
		jxf51_attr.one_line_expr_in_us = 28;
		jxf51_attr.wdr_cache = wdr_bufsize;
                jxf51_attr.min_integration_time = 1;
                jxf51_attr.min_integration_time_short = 1;
		jxf51_attr.max_integration_time_short = 0x23*2 - 3;
                jxf51_attr.max_integration_time_native = 1578*2 - 0x23*2 -6;
		jxf51_attr.integration_time_limit =  1578*2 - 0x23*2 -6;
		jxf51_attr.total_width = 2000;
		jxf51_attr.total_height = 1578*2;
		jxf51_attr.max_integration_time = 1578*2 - 0x23*2 -6;
	} else {
		ISP_ERROR("Can not support this data type!!!\n");
        }

	jxf51_attr.dbus_type = data_interface;
	jxf51_attr.max_again = 259142;
	jxf51_attr.max_dgain = 0;
	jxf51_attr.data_type = data_type;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.attr = &jxf51_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &jxf51_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->jxf51\n");

	return 0;

err_get_mclk:
	kfree(sensor);

	return -1;
}

static int jxf51_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if(reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if(pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);
    if(sensor_en != -1)
		private_gpio_free(sensor_en);

	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id jxf51_id[] = {
	{ "jxf51", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jxf51_id);

static struct i2c_driver jxf51_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "jxf51",
	},
	.probe		= jxf51_probe,
	.remove		= jxf51_remove,
	.id_table	= jxf51_id,
};

static __init int init_jxf51(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if(ret){
		ISP_ERROR("Failed to init jxf51 dirver.\n");
		return -1;
	}
	return private_i2c_add_driver(&jxf51_driver);
}

static __exit void exit_jxf51(void)
{
	private_i2c_del_driver(&jxf51_driver);
}

module_init(init_jxf51);
module_exit(exit_jxf51);

MODULE_DESCRIPTION("A low-level driver for Sonic jxf51 sensors");
MODULE_LICENSE("GPL");
