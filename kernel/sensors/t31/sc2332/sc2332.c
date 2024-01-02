/*
 * sc2332.c
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

#define SC2332_CHIP_ID_H	(0xcb)
#define SC2332_CHIP_ID_L	(0x17)
#define SC2332_REG_END		0xffff
#define SC2332_REG_DELAY	0xfffe
#define SC2332_SUPPORT_30FPS_SCLK_MIPI (76752000)
#define SC2332_SUPPORT_25FPS_SCLK_MIPI (72000000)
#define SC2332_SUPPORT_30FPS_SCLK_DVP (78000000)
#define SENSOR_OUTPUT_MAX_FPS 30
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION	"H20210309a"

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func =DVP_PA_HIGH_10BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

static int data_interface = TX_SENSOR_DATA_INTERFACE_MIPI;
module_param(data_interface, int, S_IRUGO);
MODULE_PARM_DESC(data_interface, "Sensor Date interface");

static int sensor_max_fps = TX_SENSOR_MAX_FPS_25;
module_param(sensor_max_fps, int, S_IRUGO);
MODULE_PARM_DESC(sensor_max_fps, "Sensor Max Fps set interface");

static int shvflip = 0;
module_param(shvflip, int, S_IRUGO);
MODULE_PARM_DESC(shvflip, "Sensor HV Flip Enable interface");

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sc2332_again_lut[] = {
	{0x320, 0},
	{0x322, 5776},
	{0x324, 11136},
	{0x326, 16287},
	{0x328, 21097},
	{0x32a, 25746},
	{0x32c, 30109},
	{0x32e, 34345},
	{0x330, 38336},
	{0x332, 42226},
	{0x334, 45904},
	{0x336, 49500},
	{0x338, 52910},
	{0x33a, 56254},
	{0x33c, 59433},
	{0x33e, 62558},
	{0x720, 65536},
	{0x722, 71267},
	{0x724, 76672},
	{0x726, 81784},
	{0x728, 86633},
	{0x72a, 91246},
	{0x72c, 95645},
	{0x72e, 99848},
	{0x730, 103872},
	{0x732, 107731},
	{0x734, 111440},
	{0x736, 115008},
	{0x738, 118446},
	{0x73a, 121764},
	{0x73c, 124969},
	{0x73e, 128070},
	{0xf20, 131072},
	{0xf22, 136803},
	{0xf24, 142208},
	{0xf26, 147320},
	{0xf28, 152169},
	{0xf2a, 156782},
	{0xf2c, 161181},
	{0xf2e, 165384},
	{0xf30, 169408},
	{0xf32, 173267},
	{0xf34, 176976},
	{0xf36, 180544},
	{0xf38, 183982},
	{0xf3a, 187300},
	{0xf3c, 190505},
	{0xf3e, 193606},
	{0x1f20, 196608},
	{0x1f22, 202339},
	{0x1f24, 207744},
	{0x1f26, 212856},
	{0x1f28, 217705},
	{0x1f2a, 222318},
	{0x1f2c, 226717},
	{0x1f2e, 230920},
	{0x1f30, 234944},
	{0x1f32, 238803},
	{0x1f34, 242512},
	{0x1f36, 246080},
	{0x1f38, 249518},
	{0x1f3a, 252836},
	{0x1f3c, 256041},
	{0x1f3e, 259142},
};

struct tx_isp_sensor_attribute sc2332_attr;

unsigned int sc2332_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sc2332_again_lut;
	while(lut->gain <= sc2332_attr.max_again) {
		if(isp_gain == 0) {
			*sensor_again = lut[0].value;
			return 0;
		}
		else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else{
			if((lut->gain == sc2332_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int sc2332_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_mipi_bus sc2332_mipi = {
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 360,
	.lans = 2,
	.settle_time_apative_en = 1,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.mipi_sc.line_sync_mode = 0,
	.image_twidth = 1920,
	.image_theight = 1080,
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
};

struct tx_isp_sensor_attribute sc2332_attr={
	.name = "sc2332",
	.chip_id = 0xcb17,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_16BITS,
	.cbus_device = 0x30,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
		.dvp_hcomp_en = 0,
	},
	.data_type = TX_SENSOR_DATA_TYPE_LINEAR,
	.max_again = 259142,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 1350 - 2,
	.integration_time_limit = 1350 - 2,
	.total_width = 2132,
	.total_height = 1350,
	.max_integration_time = 1350 - 2,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = sc2332_alloc_again,
	.sensor_ctrl.alloc_dgain = sc2332_alloc_dgain,
	//	void priv; /* point to struct tx_isp_sensor_board_info */
};

static struct regval_list sc2332_init_regs_1920_1080_30fps_mipi[] = {
#if 0
	{0x0103,0x01},
	{0x0100,0x00},
	{0x36e9,0x80},
	{0x36f9,0x80},
	{0x301c,0x78},
	{0x3208,0x07},
	{0x3209,0x80},
	{0x320a,0x04},
	{0x320b,0x38},
	{0x320e,0x05},
	{0x320f,0x46},/*vts 0x465 for 30fps*/
	{0x3214,0x11},
	{0x3215,0x11},
	{0x3253,0x0c},
	{0x3274,0x09},
	{0x3301,0x05},
	{0x3304,0x68},
	{0x3306,0x40},
	{0x330b,0xc8},
	{0x331c,0x01},
	{0x331e,0x61},
	{0x3333,0x10},
	{0x3364,0x17},
	{0x3391,0x18},
	{0x3392,0x38},
	{0x3393,0x08},
	{0x3394,0x0b},
	{0x3395,0x50},
	{0x3620,0x88},
	{0x3622,0x06},
	{0x3630,0xf8},
	{0x3634,0x44},
	{0x3637,0x16},
	{0x363a,0x1f},
	{0x3670,0x1c},
	{0x3677,0x84},
	{0x3678,0x86},
	{0x3679,0x8b},
	{0x367e,0x18},
	{0x367f,0x38},
	{0x3690,0x53},
	{0x3691,0x63},
	{0x3692,0x63},
	{0x369c,0x08},
	{0x369d,0x38},
	{0x36a4,0x08},
	{0x36a5,0x18},
	{0x36a8,0x08},
	{0x36a9,0x28},
	{0x36aa,0x2a},
	{0x36ea,0x1f},
	{0x36fc,0x11},
	{0x36fd,0x34},
	{0x3e01,0x8c},
	{0x3e03,0x0b},
	{0x3e08,0x03},
	{0x3e09,0x20},
	{0x3e1b,0x15},
	{0x3f03,0x02},
	{0x36e9,0x59},
	{0x36f9,0x21},
	{0x0100,0x01},
#else
	/*mipi 2lane 384Mbps*/
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301c, 0x78},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320c, 0x08},
	{0x320d, 0x54},
	{0x320e, 0x05},
	{0x320f, 0xa0},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x05},
	{0x3304, 0x68},
	{0x3306, 0x40},
	{0x330b, 0xcc},
	{0x330e, 0x38},
	{0x331c, 0x01},
	{0x331e, 0x61},
	{0x3333, 0x10},
	{0x3364, 0x17},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x0a},
	{0x3394, 0x10},
	{0x3395, 0x50},
	{0x3620, 0x88},
	{0x3622, 0x06},
	{0x3630, 0xd8},
	{0x3634, 0x44},
	{0x3637, 0x16},
	{0x363a, 0x1f},
	{0x3670, 0x1c},
	{0x3677, 0x84},
	{0x3678, 0x86},
	{0x3679, 0x8b},
	{0x367e, 0x18},
	{0x367f, 0x38},
	{0x3690, 0x53},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a4, 0x08},
	{0x36a5, 0x18},
	{0x36a8, 0x08},
	{0x36a9, 0x28},
	{0x36aa, 0x2a},
	{0x36ea, 0x38},
	{0x36eb, 0x0c},
	{0x36ec, 0x1c},
	{0x36ed, 0x14},
	{0x36fa, 0x30},
	{0x36fb, 0x00},
	{0x36fc, 0x10},
	{0x36fd, 0x17},
	{0x391d, 0x0c},
	{0x3e01, 0x8c},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x20},
	{0x3e1b, 0x15},
	{0x3f03, 0xff},
	{0x4509, 0x20},
	{0x4819, 0x05},
	{0x481b, 0x03},
	{0x481d, 0x0b},
	{0x481f, 0x02},
	{0x4821, 0x08},
	{0x4823, 0x03},
	{0x4825, 0x02},
	{0x4827, 0x03},
	{0x4829, 0x04},
	{0x578a, 0x30},
	{0x578b, 0x30},
	{0x5793, 0x18},
	{0x5794, 0x10},
	{0x36e9, 0x00},
	{0x36f9, 0x30},
	{0x0100, 0x01},
#endif
	{SC2332_REG_DELAY, 0x16},

	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc2332_init_regs_1920_1080_30fps_dvp[] = {
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc2332_init_regs_1920_1080_25fps_mipi[] = {
#if 0
	/*1080p@25fps 337.5M*/
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301c, 0x78},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320c, 0x08},
	{0x320d, 0xca},
	{0x320e, 0x04},
	{0x320f, 0xb0},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x05},
	{0x3304, 0x68},
	{0x3306, 0x40},
	{0x330b, 0xcc},
	{0x330e, 0x38},
	{0x331c, 0x01},
	{0x331e, 0x61},
	{0x3333, 0x10},
	{0x3364, 0x17},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x0a},
	{0x3394, 0x10},
	{0x3395, 0x50},
	{0x3620, 0x88},
	{0x3622, 0x06},
	{0x3630, 0xd8},
	{0x3634, 0x44},
	{0x3637, 0x16},
	{0x363a, 0x1f},
	{0x3670, 0x1c},
	{0x3677, 0x84},
	{0x3678, 0x86},
	{0x3679, 0x8b},
	{0x367e, 0x18},
	{0x367f, 0x38},
	{0x3690, 0x53},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a4, 0x08},
	{0x36a5, 0x18},
	{0x36a8, 0x08},
	{0x36a9, 0x28},
	{0x36aa, 0x2a},
	{0x36ea, 0x31},
	{0x36eb, 0x0c},
	{0x36ec, 0x1c},
	{0x36ed, 0x24},
	{0x36fa, 0xf6},
	{0x36fb, 0x00},
	{0x36fc, 0x11},
	{0x36fd, 0x04},
	{0x391d, 0x0c},
	{0x3e01, 0x95},
	{0x3e02, 0x60},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x20},
	{0x3e1b, 0x15},
	{0x3f03, 0x02},
	{0x4509, 0x20},
	{0x4819, 0x05},
	{0x481b, 0x03},
	{0x481d, 0x09},
	{0x481f, 0x02},
	{0x4821, 0x08},
	{0x4823, 0x03},
	{0x4825, 0x02},
	{0x4827, 0x03},
	{0x4829, 0x04},
	{0x578a, 0x30},
	{0x578b, 0x30},
	{0x5793, 0x18},
	{0x5794, 0x10},
	{0x36e9, 0x51},
	{0x36f9, 0x23},
	{0x0100, 0x01},
#else
	/*1080p@25fps 360Mbps*/
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301c, 0x78},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320c, 0x08},
	{0x320d, 0x54},
	{0x320e, 0x05},
	{0x320f, 0x46},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x05},
	{0x3304, 0x68},
	{0x3306, 0x40},
	{0x330b, 0xcc},
	{0x330e, 0x38},
	{0x331c, 0x01},
	{0x331e, 0x61},
	{0x3333, 0x10},
	{0x3364, 0x17},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x0a},
	{0x3394, 0x10},
	{0x3395, 0x50},
	{0x3620, 0x88},
	{0x3622, 0x06},
	{0x3630, 0xd8},
	{0x3634, 0x44},
	{0x3637, 0x16},
	{0x363a, 0x1f},
	{0x3670, 0x1c},
	{0x3677, 0x84},
	{0x3678, 0x86},
	{0x3679, 0x8b},
	{0x367e, 0x18},
	{0x367f, 0x38},
	{0x3690, 0x53},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a4, 0x08},
	{0x36a5, 0x18},
	{0x36a8, 0x08},
	{0x36a9, 0x28},
	{0x36aa, 0x2a},
	{0x36ea, 0x31},
	{0x36eb, 0x0c},
	{0x36ec, 0x1c},
	{0x36ed, 0x14},
	{0x36fa, 0x30},
	{0x36fb, 0x00},
	{0x36fc, 0x11},
	{0x36fd, 0x34},
	{0x391d, 0x0c},
	{0x3e01, 0x95},
	{0x3e02, 0x60},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x20},
	{0x3e1b, 0x15},
	{0x3f03, 0x2c},/*init setting ver id*/
	{0x4509, 0x20},
	{0x4819, 0x05},
	{0x481b, 0x03},
	{0x481d, 0x0a},
	{0x481f, 0x02},
	{0x4821, 0x08},
	{0x4823, 0x03},
	{0x4825, 0x02},
	{0x4827, 0x03},
	{0x4829, 0x04},
	{0x578a, 0x30},
	{0x578b, 0x30},
	{0x5793, 0x18},
	{0x5794, 0x10},
	{0x36e9, 0x20},
	{0x36f9, 0x20},
	{0x0100, 0x01},
#endif
	{SC2332_REG_DELAY, 0x16},
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct tx_isp_sensor_win_setting sc2332_win_sizes[] = {
	/* 1920*1080 */
	{
		.width		= 1920,
		.height		= 1080,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= sc2332_init_regs_1920_1080_30fps_dvp,
	},
	{
		.width		= 1920,
		.height		= 1080,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= sc2332_init_regs_1920_1080_30fps_mipi,
	},
	{
		.width		= 1920,
		.height		= 1080,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= sc2332_init_regs_1920_1080_25fps_mipi,
	}
};
struct tx_isp_sensor_win_setting *wsize = &sc2332_win_sizes[1];

/*
 * the part of driver was fixed.
 */

static struct regval_list sc2332_stream_on_dvp[] = {
	{0x0100, 0x01},
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc2332_stream_off_dvp[] = {
	{0x0100, 0x00},
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc2332_stream_on_mipi[] = {
	{0x0100, 0x01},
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc2332_stream_off_mipi[] = {
	{0x0100, 0x00},
	{SC2332_REG_END, 0x00},	/* END MARKER */
};

int sc2332_read(struct tx_isp_subdev *sd, uint16_t reg, unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg >> 8, reg & 0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
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

int sc2332_write(struct tx_isp_subdev *sd, uint16_t reg, unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[3] = {(reg >> 8) & 0xff, reg & 0xff, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int sc2332_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SC2332_REG_END) {
		if (vals->reg_num == SC2332_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sc2332_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}
static int sc2332_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SC2332_REG_END) {
		if (vals->reg_num == SC2332_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sc2332_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sc2332_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int sc2332_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;

	ret = sc2332_read(sd, 0x3107, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SC2332_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sc2332_read(sd, 0x3108, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != SC2332_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sc2332_set_expo(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it = (value & 0xffff) * 2;
	int again = (value & 0xffff0000) >> 16;
	ret += sc2332_write(sd, 0x3e00, (unsigned char)((it >> 12) & 0xf));
	ret += sc2332_write(sd, 0x3e01, (unsigned char)((it >> 4) & 0xff));
	ret += sc2332_write(sd, 0x3e02, (unsigned char)((it & 0x0f) << 4));
	ret = sc2332_write(sd, 0x3e09, (unsigned char)(again & 0xff));
	ret += sc2332_write(sd, 0x3e08, (unsigned char)(((again >> 8) & 0xff)));

	if (ret < 0)
		return ret;

	return 0;
}

static int sc2332_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	value *= 2;
	ret += sc2332_write(sd, 0x3e00, (unsigned char)((value >> 12) & 0x0f));
	ret += sc2332_write(sd, 0x3e01, (unsigned char)((value >> 4) & 0xff));
	ret += sc2332_write(sd, 0x3e02, (unsigned char)((value & 0x0f) << 4));


	if (ret < 0)
		return ret;

	return 0;
}

static int sc2332_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret = sc2332_write(sd, 0x3e09, (unsigned char)(value & 0xff));
	ret += sc2332_write(sd, 0x3e08, (unsigned char)((value >> 8) & 0xff));

	if (ret < 0)
		return ret;

	return 0;
}

static int sc2332_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sc2332_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sc2332_init(struct tx_isp_subdev *sd, int enable)
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

	ret = sc2332_write_array(sd, wsize->regs);

	if (ret)
		return ret;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int sc2332_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = sc2332_write_array(sd, sc2332_stream_on_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc2332_write_array(sd, sc2332_stream_on_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		ISP_WARNING("sc2332 stream on\n");

	}
	else {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = sc2332_write_array(sd, sc2332_stream_off_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc2332_write_array(sd, sc2332_stream_off_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		ISP_WARNING("sc2332 stream off\n");
	}

	return ret;
}

static int sc2332_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned char tmp = 0;
	unsigned int max_fps = 0;

	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	if(data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
		switch(sensor_max_fps){
		case TX_SENSOR_MAX_FPS_30:
			sclk = SC2332_SUPPORT_30FPS_SCLK_MIPI;
			max_fps = TX_SENSOR_MAX_FPS_30;
			break;
		case TX_SENSOR_MAX_FPS_25:
			sclk = SC2332_SUPPORT_25FPS_SCLK_MIPI;
			max_fps = TX_SENSOR_MAX_FPS_25;
			break;
		default:
			ISP_ERROR("do not support max framerate %d in mipi mode\n",sensor_max_fps);
		}
	} else if(data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
		sclk = SC2332_SUPPORT_30FPS_SCLK_DVP;
		max_fps = TX_SENSOR_MAX_FPS_30;
	}
	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (max_fps << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(0x%x) no in range\n", fps);
		return -1;
	}

	ret = sc2332_read(sd, 0x320c, &tmp);
	hts = tmp;
	ret += sc2332_read(sd, 0x320d, &tmp);
	if (0 != ret) {
		ISP_ERROR("err: sc2332 read err\n");
		return ret;
	}
	hts = ((hts << 8) + tmp);
	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);

	ret = sc2332_write(sd, 0x3812,0x00);
	ret += sc2332_write(sd, 0x320f, (unsigned char)(vts & 0xff));
	ret += sc2332_write(sd, 0x320e, (unsigned char)(vts >> 8));
	ret += sc2332_write(sd, 0x3812,0x30);
	if (0 != ret) {
		ISP_ERROR("err: sc2332_write err\n");
		return ret;
	}

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 2;
	sensor->video.attr->integration_time_limit = vts - 2;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 2;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sc2332_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = -1;
	unsigned char val = 0x0;

	ret += sc2332_read(sd, 0x3221, &val);
	if(enable & 0x2)
		val |= 0x60;
	else
		val &= 0x9f;

	ret += sc2332_write(sd, 0x3221, val);
	if(!ret)
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sc2332_set_mode(struct tx_isp_subdev *sd, int value)
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

static int sc2332_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	if(reset_gpio != -1){
		ret = private_gpio_request(reset_gpio,"sc2332_reset");
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
		ret = private_gpio_request(pwdn_gpio,"sc2332_pwdn");
		if(!ret){
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(150);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	ret = sc2332_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an sc2332 chip.\n",
			  client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("sc2332 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	ISP_WARNING("sensor driver version %s\n",SENSOR_VERSION);
	if(chip){
		memcpy(chip->name, "sc2332", sizeof("sc2332"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}

static int sc2332_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_EXPO:
		if(arg)
			ret = sc2332_set_expo(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		//if(arg)
		//	ret = sc2332_set_integration_time(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		//if(arg)
		//	ret = sc2332_set_analog_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = sc2332_set_digital_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = sc2332_get_black_pedestal(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
			ret = sc2332_set_mode(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = sc2332_write_array(sd, sc2332_stream_off_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc2332_write_array(sd, sc2332_stream_off_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		if (data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
			ret = sc2332_write_array(sd, sc2332_stream_on_dvp);
		} else if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc2332_write_array(sd, sc2332_stream_on_mipi);

		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
			ret = -1;
		}
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
			ret = sc2332_set_fps(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if(arg)
			ret = sc2332_set_vflip(sd, *(int*)arg);
		break;
	default:
		break;
	}

	return ret;
}

static int sc2332_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
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
	ret = sc2332_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int sc2332_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if(len && strncmp(sd->chip.name, reg->name, len)){
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	sc2332_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops sc2332_core_ops = {
	.g_chip_ident = sc2332_g_chip_ident,
	.reset = sc2332_reset,
	.init = sc2332_init,
	/*.ioctl = sc2332_ops_ioctl,*/
	.g_register = sc2332_g_register,
	.s_register = sc2332_s_register,
};

static struct tx_isp_subdev_video_ops sc2332_video_ops = {
	.s_stream = sc2332_s_stream,
};

static struct tx_isp_subdev_sensor_ops	sc2332_sensor_ops = {
	.ioctl	= sc2332_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops sc2332_ops = {
	.core = &sc2332_core_ops,
	.video = &sc2332_video_ops,
	.sensor = &sc2332_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "sc2332",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int sc2332_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
	int ret;

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
	/*
	rate = clk_get_rate(clk_get_parent(sensor->mclk));
	if (((rate / 1000) % 27000) != 0) {
		struct clk *vpll;
		vpll = clk_get(NULL,"vpll");
		if (IS_ERR(vpll)) {
			pr_err("get vpll failed\n");
		} else {
			rate = clk_get_rate(vpll);
			if (((rate / 1000) % 27000) != 0) {
				clk_set_rate(vpll,1080000000);
			}
			ret = clk_set_parent(sensor->mclk, vpll);
			if (ret < 0)
				pr_err("set mclk parent as epll err\n");
		}
	}
	private_clk_set_rate(sensor->mclk, 27000000);
	*/
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_enable(sensor->mclk);

	if(data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
		switch(sensor_max_fps){
		case TX_SENSOR_MAX_FPS_30:
			wsize = &sc2332_win_sizes[1];
			sc2332_attr.max_integration_time_native = 1440 - 2,
			sc2332_attr.integration_time_limit = 1440 - 2,
			sc2332_attr.total_width = 2132,
			sc2332_attr.total_height = 1440,
			sc2332_attr.max_integration_time = 1440 - 2,
			memcpy((void*)(&(sc2332_attr.mipi)),(void*)(&sc2332_mipi),sizeof(sc2332_mipi));
			break;
		case TX_SENSOR_MAX_FPS_25:
			wsize = &sc2332_win_sizes[2];
			sc2332_attr.max_integration_time_native = 1350 - 2,
			sc2332_attr.integration_time_limit = 1350 - 2,
			sc2332_attr.total_width = 2132,
			sc2332_attr.total_height = 1350,
			sc2332_attr.max_integration_time = 1350 - 2,
			memcpy((void*)(&(sc2332_attr.mipi)),(void*)(&sc2332_mipi),sizeof(sc2332_mipi));
			break;
		default:
			ISP_ERROR("do not support max framerate %d in mipi mode\n",sensor_max_fps);
		}
	} else if(data_interface == TX_SENSOR_DATA_INTERFACE_DVP){
		wsize = &sc2332_win_sizes[0];
		ret = set_sensor_gpio_function(sensor_gpio_func);
		if (ret < 0)
			goto err_set_sensor_gpio;
		sc2332_attr.dvp.gpio = sensor_gpio_func;
	} else {
		ISP_ERROR("Can not support this data interface and fps!!!\n");
		goto err_set_sensor_data_interface;
	}

	sc2332_attr.dbus_type = data_interface;
	/*
	  convert sensor-gain into isp-gain,
	*/
	sc2332_attr.max_again = 259142;
	sc2332_attr.max_dgain = 0;
	sc2332_attr.expo_fs = 1;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.shvflip = shvflip;
	sensor->video.attr = &sc2332_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sc2332_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->sc2332\n");

	return 0;
err_set_sensor_data_interface:
err_set_sensor_gpio:
	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
err_get_mclk:
	kfree(sensor);

	return -1;
}

static int sc2332_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if(reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if(pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id sc2332_id[] = {
	{ "sc2332", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc2332_id);

static struct i2c_driver sc2332_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "sc2332",
	},
	.probe		= sc2332_probe,
	.remove		= sc2332_remove,
	.id_table	= sc2332_id,
};

static __init int init_sc2332(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if(ret){
		ISP_ERROR("Failed to init sc2332 dirver.\n");
		return -1;
	}
	return private_i2c_add_driver(&sc2332_driver);
}

static __exit void exit_sc2332(void)
{
	private_i2c_del_driver(&sc2332_driver);
}

module_init(init_sc2332);
module_exit(exit_sc2332);

MODULE_DESCRIPTION("A low-level driver for SmartSens sc2332 sensors");
MODULE_LICENSE("GPL");
