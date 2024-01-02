/*
 * sc1346.c
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
#include <txx-funcs.h>

#define sc1346_CHIP_ID_H	(0xda)
#define sc1346_CHIP_ID_L	(0x4d)
#define sc1346_REG_END		0xffff
#define sc1346_REG_DELAY	0xfffe
#define sc1346_SUPPORT_15FPS_SCLK (20250000)
#define SENSOR_OUTPUT_MAX_FPS 15
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION	"H20220623-1a"

static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int data_interface = TX_SENSOR_DATA_INTERFACE_MIPI;
module_param(data_interface, int, S_IRUGO);
MODULE_PARM_DESC(data_interface, "Sensor Date interface");

static int shvflip = 0;
module_param(shvflip, int, S_IRUGO);
MODULE_PARM_DESC(shvflip, "Sensor HV Flip Enable interface");

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

//static unsigned short int dpc_flag = 1;
//static unsigned int gain_val = 0x37e;
/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut sc1346_again_lut[] = {
	{0x80, 0},
	{0x84, 2886},
	{0x88, 5776},
	{0x8c, 8494},
	{0x90, 11136},
	{0x94, 13706},
	{0x98, 16287},
	{0x9c, 18723},
	{0xa0, 21097},
	{0xa4, 23413},
	{0xa8, 25746},
	{0xac, 27952},
	{0xb0, 30108},
	{0xb4, 32216},
	{0xb8, 34344},
	{0xbc, 36361},
	{0xc0, 38335},
	{0xc4, 40269},
	{0xc8, 42225},
	{0xcc, 44082},
	{0xd0, 45903},
	{0xd4, 47689},
	{0xd8, 49499},
	{0xdc, 51220},
	{0xe0, 52910},
	{0xe4, 54570},
	{0xe8, 56253},
	{0xec, 57856},
	{0xf0, 59433},
	{0xf4, 60983},
	{0xf8, 62557},
	{0xfc, 64058},
	{0x880, 65535},
	{0x884, 68421},
	{0x888, 71311},
	{0x88c, 74029},
	{0x890, 76671},
	{0x894, 79241},
	{0x898, 81822},
	{0x89c, 84258},
	{0x8a0, 86632},
	{0x8a4, 88948},
	{0x8a8, 91281},
	{0x8ac, 93487},
	{0x8b0, 95643},
	{0x8b4, 97751},
	{0x8b8, 99879},
	{0x8bc, 101896},
	{0x8c0, 103870},
	{0x8c4, 105804},
	{0x8c8, 107760},
	{0x8cc, 109617},
	{0x8d0, 111438},
	{0x8d4, 113224},
	{0x8d8, 115034},
	{0x8dc, 116755},
	{0x8e0, 118445},
	{0x8e4, 120105},
	{0x8e8, 121788},
	{0x8ec, 123391},
	{0x8f0, 124968},
	{0x8f4, 126518},
	{0x8f8, 128092},
	{0x8fc, 129593},
	{0x980, 131070},
	{0x984, 133956},
	{0x988, 136846},
	{0x98c, 139564},
	{0x990, 142206},
	{0x994, 144776},
	{0x998, 147357},
	{0x99c, 149793},
	{0x9a0, 152167},
	{0x9a4, 154483},
	{0x9a8, 156816},
	{0x9ac, 159022},
	{0x9b0, 161178},
	{0x9b4, 163286},
	{0x9b8, 165414},
	{0x9bc, 167431},
	{0x9c0, 169405},
	{0x9c4, 171339},
	{0x9c8, 173295},
	{0x9cc, 175152},
	{0x9d0, 176973},
	{0x9d4, 178759},
	{0x9d8, 180569},
	{0x9dc, 182290},
	{0x9e0, 183980},
	{0x9e4, 185640},
	{0x9e8, 187323},
	{0x9ec, 188926},
	{0x9f0, 190503},
	{0x9f4, 192053},
	{0x9f8, 193627},
	{0x9fc, 195128},
	{0xb80, 196605},
	{0xb84, 199491},
	{0xb88, 202381},
	{0xb8c, 205099},
	{0xb90, 207741},
	{0xb94, 210311},
	{0xb98, 212892},
	{0xb9c, 215328},
	{0xba0, 217702},
	{0xba4, 220018},
	{0xba8, 222351},
	{0xbac, 224557},
	{0xbb0, 226713},
	{0xbb4, 228821},
	{0xbb8, 230949},
	{0xbbc, 232966},
	{0xbc0, 234940},
	{0xbc4, 236874},
	{0xbc8, 238830},
	{0xbcc, 240687},
	{0xbd0, 242508},
	{0xbd4, 244294},
	{0xbd8, 246104},
	{0xbdc, 247825},
	{0xbe0, 249515},
	{0xbe4, 251175},
	{0xbe8, 252858},
	{0xbec, 254461},
	{0xbf0, 256038},
	{0xbf4, 257588},
	{0xbf8, 259162},
	{0xbfc, 260663},
	{0xf80, 262140},
	{0xf84, 265026},
	{0xf8c, 270634},
	{0xf90, 273276},
	{0xf94, 275846},
	{0xf98, 278427},
	{0xf9c, 280863},
	{0xfa0, 283237},
	{0xfa4, 285553},
	{0xfa8, 287886},
	{0xfac, 290092},
	{0xfb0, 292248},
	{0xfb4, 294356},
	{0xfb8, 296484},
	{0xfbc, 298501},
	{0xfc0, 300475},
	{0xfc4, 302409},
	{0xfc8, 304365},
	{0xfcc, 306222},
	{0xfd0, 308043},
	{0xfd4, 309829},
	{0xfd8, 311639},
	{0xfdc, 313360},
	{0xfe0, 315050},
	{0xfe4, 316710},
	{0xfe8, 318393},
	{0xfec, 319996},
	{0xff0, 321573},
	{0xff4, 323123},
	{0xff8, 324697},
	{0xffc, 326198},
	{0x1f80, 327675},
	{0x1f84, 330561},
	{0x1f88, 333451},
	{0x1f8c, 336169},
	{0x1f90, 338811},
	{0x1f94, 341381},
	{0x1f98, 343962},
	{0x1f9c, 346398},
	{0x1fa0, 348772},
	{0x1fa4, 351088},
	{0x1fa8, 353421},
	{0x1fac, 355627},
	{0x1fb0, 357783},
	{0x1fb4, 359891},
	{0x1fb8, 362019},
	{0x1fbc, 364036},
	{0x1fc0, 366010},
	{0x1fc4, 367944},
	{0x1fc8, 369900},
	{0x1fcc, 371757},
	{0x1fd0, 373578},
	{0x1fd4, 375364},
	{0x1fd8, 377174},
	{0x1fdc, 378895},
	{0x1fe0, 380585},
	{0x1fe4, 382245},
	{0x1fe8, 383928},
	{0x1fec, 385531},
	{0x1ff0, 387108},
	{0x1ff4, 388658},
	{0x1ff8, 390232},
	{0x1ffc, 391733},
};
struct tx_isp_sensor_attribute sc1346_attr;

unsigned int sc1346_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = sc1346_again_lut;
	while(lut->gain <= sc1346_attr.max_again) {
		if(isp_gain == 0) {
			*sensor_again = lut[0].value;
			return   lut[0].gain;
		} else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		} else {
			if((lut->gain == sc1346_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int sc1346_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_sensor_attribute sc1346_attr={
	.name = "sc1346",
	.chip_id = 0xda4d,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_16BITS,
	.cbus_device = 0x30,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.mipi = {
		.mode = SENSOR_MIPI_OTHER_MODE,
		.clk = 225,
		.lans = 1,
		.settle_time_apative_en = 0,
		.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
		.mipi_sc.hcrop_diff_en = 0,
		.mipi_sc.mipi_vcomp_en = 0,
		.mipi_sc.mipi_hcomp_en = 0,
		.mipi_sc.line_sync_mode = 0,
		.mipi_sc.work_start_flag = 0,
		.image_twidth = 1280,
		.image_theight = 720,
		.mipi_sc.mipi_crop_start0x = 0,
		.mipi_sc.mipi_crop_start0y = 0,
		.mipi_sc.mipi_crop_start1x = 0,
		.mipi_sc.mipi_crop_start1y = 0,
		.mipi_sc.mipi_crop_start2x = 0,
		.mipi_sc.mipi_crop_start2y = 0,
		.mipi_sc.mipi_crop_start3x = 0,
		.mipi_sc.mipi_crop_start3y = 0,
		.mipi_sc.data_type_en = 0,
		.mipi_sc.data_type_value = RAW10,
		.mipi_sc.del_start = 0,
		.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
		.mipi_sc.sensor_fid_mode = 0,
		.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
	},
	.data_type = TX_SENSOR_DATA_TYPE_LINEAR,
	.max_again = 391733,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 0x2ee -6,
	.integration_time_limit = 0x2ee -6,
	.total_width = 0x7d0,
	.total_height = 0x2ee,
	.max_integration_time = 0x2ee -6,
	.one_line_expr_in_us = 29,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = sc1346_alloc_again,
	.sensor_ctrl.alloc_dgain = sc1346_alloc_dgain,
};

static struct regval_list sc1346_init_regs_1280_720_15fps_mipi[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x36e9,0x80},
	{0x37f9,0x80},
	{0x301f,0x0b},
	{0x3106,0x05},
	{0x3200,0x00},
	{0x3201,0x00},
	{0x3202,0x00},
	{0x3203,0x00},
	{0x3204,0x05},
	{0x3205,0x07},
	{0x3206,0x02},
	{0x3207,0xdb},
	{0x3208,0x05},
	{0x3209,0x00},
	{0x320a,0x02},
	{0x320b,0xd0},
	{0x320c,0x07},
	{0x320d,0xd0},
	{0x320e,0x02},
	{0x320f,0xee},
	{0x3210,0x00},
	{0x3211,0x04},
	{0x3212,0x00},
	{0x3213,0x04},
	{0x3250,0x00},
	{0x3301,0x06},
	{0x3306,0x50},
	{0x3308,0x0a},
	{0x330a,0x00},
	{0x330b,0xda},
	{0x330e,0x0a},
	{0x331e,0x61},
	{0x331f,0xa1},
	{0x3364,0x1f},
	{0x3390,0x09},
	{0x3391,0x0f},
	{0x3392,0x1f},
	{0x3393,0x30},
	{0x3394,0x30},
	{0x3395,0x30},
	{0x33ad,0x10},
	{0x33b3,0x40},
	{0x33f9,0x50},
	{0x33fb,0x70},
	{0x33fc,0x09},
	{0x33fd,0x0f},
	{0x349f,0x03},
	{0x34a6,0x09},
	{0x34a7,0x0f},
	{0x34a8,0x40},
	{0x34a9,0x30},
	{0x34aa,0x00},
	{0x34ab,0xe8},
	{0x34ac,0x00},
	{0x34ad,0xfc},
	{0x3630,0xe2},
	{0x3632,0x76},
	{0x3633,0x33},
	{0x3639,0xf4},
	{0x3670,0x09},
	{0x3674,0xe2},
	{0x3675,0xea},
	{0x3676,0xea},
	{0x367c,0x09},
	{0x367d,0x0f},
	{0x3690,0x22},
	{0x3691,0x22},
	{0x3692,0x22},
	{0x3698,0x88},
	{0x3699,0x90},
	{0x369a,0xa1},
	{0x369b,0xc3},
	{0x369c,0x09},
	{0x369d,0x0f},
	{0x36a2,0x09},
	{0x36a3,0x0b},
	{0x36a4,0x0f},
	{0x36d0,0x01},
	{0x36ea,0x0f},
	{0x36eb,0x0d},
	{0x36ec,0x15},
	{0x36ed,0x28},
	{0x370f,0x01},
	{0x3722,0x41},
	{0x3724,0x41},
	{0x3725,0xc1},
	{0x3728,0x00},
	{0x37b0,0x41},
	{0x37b1,0x41},
	{0x37b2,0x47},
	{0x37b3,0x09},
	{0x37b4,0x0f},
	{0x37fa,0x05},
	{0x37fb,0x33},
	{0x37fc,0x11},
	{0x37fd,0x37},
	{0x3903,0x40},
	{0x3904,0x04},
	{0x3905,0x8d},
	{0x3907,0x00},
	{0x3908,0x41},
	{0x3933,0x80},
	{0x3934,0x0a},
	{0x3937,0x79},
	{0x3939,0x00},
	{0x393a,0x00},
	{0x3e00,0x00},
	{0x3e01,0x2e},
	{0x3e02,0x80},
	{0x440e,0x02},
	{0x4509,0x20},
	{0x450d,0x28},
	{0x4800,0x64},
	{0x4819,0x04},
	{0x481b,0x02},
	{0x481d,0x06},
	{0x481f,0x02},
	{0x4821,0x07},
	{0x4823,0x02},
	{0x4825,0x02},
	{0x4827,0x02},
	{0x4829,0x03},
	{0x36e9,0x28},
	{0x37f9,0x20},
	{0x0100,0x01},

	{sc1346_REG_END, 0x00},	/* END MARKER */
};

static struct tx_isp_sensor_win_setting sc1346_win_sizes[] = {
	{
		.width		= 1280,
		.height		= 720,
		.fps		= 15 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= sc1346_init_regs_1280_720_15fps_mipi,
	},
};
struct tx_isp_sensor_win_setting *wsize = &sc1346_win_sizes[0];

static struct regval_list sc1346_stream_on_mipi[] = {
	{0x0100, 0x01},
	{sc1346_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc1346_stream_off_mipi[] = {
	{0x0100, 0x00},
	{sc1346_REG_END, 0x00},	/* END MARKER */
};

int sc1346_read(struct tx_isp_subdev *sd, uint16_t reg, unsigned char *value)
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

int sc1346_write(struct tx_isp_subdev *sd, uint16_t reg, unsigned char value)
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

static int sc1346_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != sc1346_REG_END) {
		if (vals->reg_num == sc1346_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sc1346_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sc1346_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != sc1346_REG_END) {
		if (vals->reg_num == sc1346_REG_DELAY) {
			private_msleep(vals->value);
		} else {
			ret = sc1346_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int sc1346_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int sc1346_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;

	ret = sc1346_read(sd, 0x3107, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != sc1346_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sc1346_read(sd, 0x3108, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != sc1346_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static int sc1346_set_expo(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it =( value & 0xffff);
	int again = (value & 0xffff0000) >> 16;
	ret += sc1346_write(sd, 0x3e00, (unsigned char)((it >> 12) & 0xf));
	ret += sc1346_write(sd, 0x3e01, (unsigned char)((it >> 4) & 0xff));
	ret += sc1346_write(sd, 0x3e02, (unsigned char)((it & 0x0f) << 4));
	ret = sc1346_write(sd, 0x3e07, (unsigned char)(again & 0xff));
	ret += sc1346_write(sd, 0x3e09, (unsigned char)(((again >> 8) & 0xff)));

	if (ret < 0)
		return ret;
//	gain_val = again;

	return 0;
}
#if 0
static int sc1346_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret += sc1346_write(sd, 0x3e00, (unsigned char)((value >> 12) & 0xf));
	ret += sc1346_write(sd, 0x3e01, (unsigned char)((value >> 4) & 0xff));
	ret += sc1346_write(sd, 0x3e02, (unsigned char)((value & 0x0f) << 4));
	if (ret < 0)
		return ret;

	return 0;
}

static int sc1346_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;

	ret += sc1346_write(sd, 0x3e07, (unsigned char)(value & 0xff));
	ret += sc1346_write(sd, 0x3e09, (unsigned char)(((value >> 8) & 0xff)));
	if (ret < 0)
		return ret;

	return 0;
}
#endif

static int sc1346_set_logic(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sc1346_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sc1346_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int sc1346_init(struct tx_isp_subdev *sd, int enable)
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

	ret = sc1346_write_array(sd, wsize->regs);
	if (ret)
		return ret;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int sc1346_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc1346_write_array(sd, sc1346_stream_on_mipi);
		} else {
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		ISP_WARNING("sc1346 stream on\n");
	}
	else {
		if (data_interface == TX_SENSOR_DATA_INTERFACE_MIPI){
			ret = sc1346_write_array(sd, sc1346_stream_off_mipi);
		}else{
			ISP_ERROR("Don't support this Sensor Data interface\n");
		}
		ISP_WARNING("sc1346 stream off\n");
	}

	return ret;
}

static int sc1346_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned char tmp = 0;

	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%d) no in range\n", fps);
		return -1;
	}
	sclk = sc1346_SUPPORT_15FPS_SCLK;
	ret = sc1346_read(sd, 0x320c, &tmp);
	hts = tmp;
	ret += sc1346_read(sd, 0x320d, &tmp);
	if (0 != ret) {
		ISP_ERROR("err: sc1346 read err\n");
		return ret;
	}
	hts = (hts << 8) + tmp;
	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);

	ret += sc1346_write(sd, 0x320f, (unsigned char)(vts & 0xff));
	ret += sc1346_write(sd, 0x320e, (unsigned char)(vts >> 8));
	if (0 != ret) {
		ISP_ERROR("err: sc1346_write err\n");
		return ret;
	}

	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 6;
	sensor->video.attr->integration_time_limit = vts - 6;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 6;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
  printk("....vts=%d\n",vts);
  printk(".....ret=%d\n",ret);
	return ret;
}

static int sc1346_set_mode(struct tx_isp_subdev *sd, int value)
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
static int sc1346_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = -1;
	unsigned char val = 0x0;

	ret += sc1346_read(sd, 0x3221, &val);

	if(enable & 0x2)
		val |= 0x60;
	else
		val &= 0x9f;

	ret += sc1346_write(sd, 0x3221, val);
	if(!ret)
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}
static int sc1346_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	if(reset_gpio != -1){
		ret = private_gpio_request(reset_gpio,"sc1346_reset");
		if(!ret){
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(10);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",reset_gpio);
		}
	}
	if(pwdn_gpio != -1){
		ret = private_gpio_request(pwdn_gpio,"sc1346_pwdn");
		if(!ret){
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(10);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	ret = sc1346_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an sc1346 chip.\n",
			  client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("sc1346 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	ISP_WARNING("sensor driver version %s\n",SENSOR_VERSION);
	if(chip){
		memcpy(chip->name, "sc1346", sizeof("sc1346"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}



static int sc1346_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_EXPO:
		if(arg)
	     	ret = sc1346_set_expo(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
	//	if(arg)
	//		ret = sc1346_set_integration_time(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
	//	if(arg)
	//		ret = sc1346_set_analog_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = sc1346_set_digital_gain(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = sc1346_get_black_pedestal(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
			ret = sc1346_set_mode(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		if(arg)
			ret = sc1346_write_array(sd, sc1346_stream_off_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		if(arg)
			ret = sc1346_write_array(sd, sc1346_stream_on_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
			ret = sc1346_set_fps(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if(arg)
			ret = sc1346_set_vflip(sd, *(int*)arg);
		break;
	case TX_ISP_EVENT_SENSOR_LOGIC:
		if(arg)
			ret = sc1346_set_logic(sd, *(int*)arg);
	default:
		break;
	}
	return ret;
}

static int sc1346_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
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
	ret = sc1346_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int sc1346_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if(len && strncmp(sd->chip.name, reg->name, len)){
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;

	sc1346_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops sc1346_core_ops = {
	.g_chip_ident = sc1346_g_chip_ident,
	.reset = sc1346_reset,
	.init = sc1346_init,
	/*.ioctl = sc1346_ops_ioctl,*/
	.g_register = sc1346_g_register,
	.s_register = sc1346_s_register,
};

static struct tx_isp_subdev_video_ops sc1346_video_ops = {
	.s_stream = sc1346_s_stream,
};

static struct tx_isp_subdev_sensor_ops	sc1346_sensor_ops = {
	.ioctl	= sc1346_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops sc1346_ops = {
	.core = &sc1346_core_ops,
	.video = &sc1346_video_ops,
	.sensor = &sc1346_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "sc1346",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int sc1346_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;

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

	sc1346_attr.max_integration_time_native = 0x2ee - 6;
	sc1346_attr.integration_time_limit = 0x2ee - 6;
	sc1346_attr.total_height = 0x2ee;
	sc1346_attr.max_integration_time = 0x2ee - 6;

	/*
	  convert sensor-gain into isp-gain,
	*/
	sc1346_attr.max_again = 391733;
	sc1346_attr.max_dgain = 0 ;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.shvflip = shvflip;
	sc1346_attr.expo_fs=1;
	sensor->video.attr = &sc1346_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &sc1346_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	printk("---------->> integration_time: %d\n", sensor->video.attr->max_integration_time);

	ISP_WARNING("\n probe ok ------->sc1346\n");

	return 0;

err_get_mclk:
	private_clk_disable(sensor->mclk);
	private_clk_put(sensor->mclk);
	kfree(sensor);

	return -1;
}

static int sc1346_remove(struct i2c_client *client)
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

static const struct i2c_device_id sc1346_id[] = {
	{ "sc1346", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc1346_id);

static struct i2c_driver sc1346_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "sc1346",
	},
	.probe		= sc1346_probe,
	.remove		= sc1346_remove,
	.id_table	= sc1346_id,
};

static __init int init_sc1346(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if(ret){
		ISP_ERROR("Failed to init sc1346 dirver.\n");
		return -1;
	}
	return private_i2c_add_driver(&sc1346_driver);
}

static __exit void exit_sc1346(void)
{
	private_i2c_del_driver(&sc1346_driver);
}

module_init(init_sc1346);
module_exit(exit_sc1346);

MODULE_DESCRIPTION("A low-level driver for SmartSens sc1346 sensors");
MODULE_LICENSE("GPL");
