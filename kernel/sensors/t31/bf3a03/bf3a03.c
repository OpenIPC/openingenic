/*
 * bf3a03.c
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
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <soc/gpio.h>
#include <tx-isp-common.h>
#include <sensor-common.h>
#include <txx-funcs.h>

#define BF3A03_CHIP_ID_H	(0x3a)
#define BF3A03_CHIP_ID_L	(0x03)

#define BF3A03_FLAG_END		0xff
#define BF3A03_FLAG_DELAY	0xfe
#define BF3A03_PAGE_REG		0xfa

#define BF3A03_SUPPORT_PCLK (12*1000*1000)
#define SENSOR_OUTPUT_MAX_FPS 25
#define SENSOR_OUTPUT_MIN_FPS 10
#define DRIVE_CAPABILITY_2
#define SENSOR_VERSION	"H20200116a"

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};
static int reset_gpio = GPIO_PA(18);
module_param(reset_gpio, int, S_IRUGO);
MODULE_PARM_DESC(reset_gpio, "Reset GPIO NUM");

static int pwdn_gpio = GPIO_PA(19);
module_param(pwdn_gpio, int, S_IRUGO);
MODULE_PARM_DESC(pwdn_gpio, "Power down GPIO NUM");

static int sensor_gpio_func = DVP_PA_LOW_8BIT;
module_param(sensor_gpio_func, int, S_IRUGO);
MODULE_PARM_DESC(sensor_gpio_func, "Sensor GPIO function");

struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut bf3a03_again_lut[] = {
	{0x0, 0},
	{0x1, 5731},
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
	{0x21, 133981},
	{0x22, 136803},
	{0x23, 139544},
	{0x24, 142208},
	{0x25, 144798},
	{0x26, 147320},
	{0x27, 149776},
	{0x28, 152169},
	{0x29, 154504},
	{0x2a, 156782},
	{0x2b, 159007},
	{0x2c, 161181},
	{0x2d, 163306},
	{0x2e, 165384},
	{0x2f, 167417},
	{0x30, 169408},
	{0x31, 171357},
	{0x32, 173267},
	{0x33, 175140},
	{0x34, 176976},
	{0x35, 178776},
	{0x36, 180544},
	{0x37, 182279},
	{0x38, 183982},
	{0x39, 185656},
	{0x3a, 187300},
	{0x3b, 188916},
	{0x3c, 190505},
	{0x3d, 192068},
	{0x3e, 193606},
	{0x3f, 195119},
};

struct tx_isp_sensor_attribute bf3a03_attr;

unsigned int bf3a03_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = bf3a03_again_lut;

	while(lut->gain <= bf3a03_attr.max_again) {
		if(isp_gain == bf3a03_attr.max_again) {
			*sensor_again = lut->value;
			return lut->gain;
		}
		else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else{
			if(lut->gain == bf3a03_attr.max_again) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int bf3a03_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}
/*
 * the part of driver maybe modify about different sensor and different board.
 */

struct tx_isp_sensor_attribute bf3a03_attr={
	.name = "bf3a03",
	.chip_id = 0x3a03,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS | V4L2_SBUS_MASK_ADDR_8BITS,
	.cbus_device = 0x6e,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP,
	.dvp = {
		.mode = SENSOR_DVP_HREF_MODE,
		.blanking = {
			.vblanking = 0,
			.hblanking = 0,
		},
	},
	//.expo_fs = 1,
	.max_again = 195119,
	.max_dgain = 0,
	.total_width = 816,
	.total_height = 510,
	.min_integration_time = 4,
	.max_integration_time = 610 - 4,
	.integration_time_limit = 510 - 4,
	.min_integration_time_native = 4,
	.max_integration_time_native = 510 - 4,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 2,
	.sensor_ctrl.alloc_again = bf3a03_alloc_again,
	.sensor_ctrl.alloc_dgain = bf3a03_alloc_dgain,
};

static struct regval_list bf3a03_init_regs_640_480[] = {
	//	{0x12,0x80},
	{0x09,0x55},
	{0x15,0x02},
	{0x1e,0x40},
	{0x06,0x78},
	{0x21,0x00},
	{0x3e,0x37},
	{0x29,0x2b},
	{0x27,0x98},
	{0x16,0x25},
	{0xd9,0x24},
	{0xdf,0x2f},
	{0x96,0xb5},
	{0xf7,0x8a},
	{0x20,0x00},
	{0xd2,0x18},
	{0x11,0x10},
	{0x1b,0x09},
	{0x2f,0x4e},
	{0x2b,0x20},
	{0x92,0x09},
	{0xcc,0x77},
	{0xcd,0x7b},
	{0xce,0x82},
	{0xcf,0x98},
	{0xc3,0xa0},
	{0x3a,0x00},
	{0x4a,0x98},
	{0x12,0x01},
	{0x0c,0x80},
	{0xf1,0x6b},
	{0x3b,0x00},
	{0x70,0x0d},
	{0x71,0x08},
	{0x73,0x2d},
	{0x74,0x6b},
	{0x75,0xa8},
	{0x76,0xd0},
	{0x77,0x0a},
	{0x78,0xff},
	{0x79,0x14},
	{0x7a,0x24},
	{0x9e,0x04},
	{0x7d,0x2a},
	{0x13,0x08},
	{0x6a,0x81},
	{0x23,0x33},
	{0x01,0x0b},
	{0x02,0x0b},
	{0x8c,0x02},
	//	{0x8d,0x4c},
	{0x8d,0x64},//jz
	{0x87,0x16},
	//	{0x87,0xff},//jz
	{BF3A03_FLAG_END, 0x00},/* END MARKER */
};

/*
 * the order of the bf3a03_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting bf3a03_win_sizes[] = {
	{
		.width		= 640,
		.height		= 480,
		.fps		= 25 << 16 | 1,
		.mbus_code	= V4L2_MBUS_FMT_SBGGR8_1X8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= bf3a03_init_regs_640_480,
	}
};

/*
 * the part of driver was fixed.
 */

static struct regval_list bf3a03_stream_on[] = {
	{0x12, 0x05},
	{0x13, 0x48},
	{0x99, 0x40},
	{BF3A03_FLAG_END, 0x00},	/* END MARKER */
};

static struct regval_list bf3a03_stream_off[] = {
	{BF3A03_FLAG_END, 0x00},	/* END MARKER */
};

int bf3a03_read(struct tx_isp_subdev *sd, unsigned char reg,
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

static int bf3a03_write(struct tx_isp_subdev *sd, unsigned char reg,
		unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= buf,
	};
	int ret;
	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int bf3a03_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != BF3A03_FLAG_END) {
		if(vals->reg_num == BF3A03_FLAG_DELAY) {
			msleep(vals->value);
		} else {
			ret = bf3a03_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}
static int bf3a03_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != BF3A03_FLAG_END) {
		if (vals->reg_num == BF3A03_FLAG_DELAY) {
			msleep(vals->value);
		} else {
			ret = bf3a03_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}

static int bf3a03_reset(struct tx_isp_subdev *sd, int val)
{
	return 0;
}

static int bf3a03_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = bf3a03_read(sd, 0xfc, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != BF3A03_CHIP_ID_H)
		return -ENODEV;
	*ident = v;
	ret = bf3a03_read(sd, 0xfd, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != BF3A03_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;
	return 0;
}

static int ag_last = -1;
static int it_last = -1;
static int bf3a03_set_expo(struct tx_isp_subdev* sd, int value)
{
	return 0;
	int ret = 0;
	int expo = (value & 0x0000ffff);
	int again = (value & 0xffff0000) >> 16;

	if (ag_last != again) {
		ret = bf3a03_write(sd, 0x87, (unsigned char)((value) & 0xff));
		if (ret < 0) {
			printk("bf3a03_write analog gain error\n");
			return ret;
		}
	}

	return ret;
}

static int bf3a03_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int bf3a03_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	ret = bf3a03_write(sd, 0x87, (unsigned char)((value) & 0xff));
	if (ret < 0) {
		printk("bf3a03_write analog gain error\n");
		return ret;
	}
	return 0;
}

static int bf3a03_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int bf3a03_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int bf3a03_init(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = &bf3a03_win_sizes[0];
	int ret = 0;
	if (!enable)
		return ISP_SUCCESS;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	ret = bf3a03_write_array(sd, wsize->regs);
	if (ret)
		return ret;

	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;
	return 0;
}

static int bf3a03_s_stream(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	if (enable) {
		ret = bf3a03_write_array(sd, bf3a03_stream_on);
		pr_debug("bf3a03 stream on\n");
	}
	else {
		ret = bf3a03_write_array(sd, bf3a03_stream_off);
		pr_debug("bf3a03 stream off\n");
	}
	return ret;
}

static int bf3a03_set_fps(struct tx_isp_subdev *sd, int fps)
{
	int ret = 0;
	int vts = 0;
	unsigned int newformat = 0;
	unsigned int pclk = BF3A03_SUPPORT_PCLK;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)){
		ISP_WARNING("set_fps error ,should be %d  ~ %d \n",SENSOR_OUTPUT_MIN_FPS, SENSOR_OUTPUT_MAX_FPS);
		return -1;
	}

	switch(newformat >> 8)
	{
		case 25: vts = 0x0264; break;
		case 20: vts = 0x02fd; break;
		case 15: vts = 0x0396; break;
		case 10: vts = 0x0561; break;
		default: vts = -1; break;
	}

	if(vts == -1)
		return -1;

	ret = bf3a03_write(sd, 0x8d, vts & 0x000000ff);
	ret += bf3a03_write(sd, 0x8c, (vts >> 8) & 0x000000ff);

	if(ret < 0)
		return -1;

	sensor->video.fps = fps;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 4;
	sensor->video.attr->integration_time_limit = vts - 4;
	sensor->video.attr->max_integration_time_native = vts - 4;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	return ret;
}

static int bf3a03_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_win_setting *wsize = NULL;
	int ret = ISP_SUCCESS;
	wsize = &bf3a03_win_sizes[0];

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
static int bf3a03_g_chip_ident(struct tx_isp_subdev *sd,
		struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	if(reset_gpio != -1){
		ret = private_gpio_request(reset_gpio,"bf3a03_reset");
		if(!ret){
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(20);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(20);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",reset_gpio);
		}
	}
	/*
	   if(pwdn_gpio != -1){
	   ret = private_gpio_request(pwdn_gpio,"bf3a03_pwdn");
	   if(!ret){
	   private_gpio_direction_output(pwdn_gpio, 1);
	   private_msleep(10);
	   private_gpio_direction_output(pwdn_gpio, 0);
	   private_msleep(10);
	   }else{
	   ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
	   }
	   }
	 */
	ret = bf3a03_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an bf3a03 chip.\n",
				client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("bf3a03 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	if(chip){
		memcpy(chip->name, "bf3a03", sizeof("bf3a03"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}
static int bf3a03_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
#if 0
		case TX_ISP_EVENT_SENSOR_EXPO:
			if(arg)
				ret = bf3a03_set_expo(sd, *(int*)arg);
			break;
#else
		case TX_ISP_EVENT_SENSOR_INT_TIME:
			if(arg)
				ret = bf3a03_set_integration_time(sd, *(int*)arg);
			break;
		case TX_ISP_EVENT_SENSOR_AGAIN:
			if(arg)
				ret = bf3a03_set_analog_gain(sd, *(int*)arg);
			break;
#endif
		case TX_ISP_EVENT_SENSOR_DGAIN:
			if(arg)
				ret = bf3a03_set_digital_gain(sd, *(int*)arg);
			break;
		case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
			if(arg)
				ret = bf3a03_get_black_pedestal(sd, *(int*)arg);
			break;
		case TX_ISP_EVENT_SENSOR_RESIZE:
			if(arg)
				ret = bf3a03_set_mode(sd, *(int*)arg);
			break;
		case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
			ret = bf3a03_write_array(sd, bf3a03_stream_off);
			break;
		case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
			ret = bf3a03_write_array(sd, bf3a03_stream_on);
			break;
		case TX_ISP_EVENT_SENSOR_FPS:
			if(arg)
				ret = bf3a03_set_fps(sd, *(int*)arg);
			break;
		default:
			break;
	}
	return 0;
}

static int bf3a03_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
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
	ret = bf3a03_write(sd, 0xfe, 0x00);
	ret = bf3a03_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;
	return ret;
}

static int bf3a03_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if(len && strncmp(sd->chip.name, reg->name, len)){
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	bf3a03_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}

static struct tx_isp_subdev_core_ops bf3a03_core_ops = {
	.g_chip_ident = bf3a03_g_chip_ident,
	.reset = bf3a03_reset,
	.init = bf3a03_init,
	.g_register = bf3a03_g_register,
	.s_register = bf3a03_s_register,
};

static struct tx_isp_subdev_video_ops bf3a03_video_ops = {
	.s_stream = bf3a03_s_stream,
};

static struct tx_isp_subdev_sensor_ops	bf3a03_sensor_ops = {
	.ioctl	= bf3a03_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops bf3a03_ops = {
	.core = &bf3a03_core_ops,
	.video = &bf3a03_video_ops,
	.sensor = &bf3a03_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "bf3a03",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int bf3a03_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret;
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_win_setting *wsize = &bf3a03_win_sizes[0];

	it_last = -1;
	ag_last = -1;
	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if(!sensor){
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0 ,sizeof(*sensor));
	/* request mclk of sensor */
	sensor->mclk = clk_get(NULL, "cgu_cim");
	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_enable(sensor->mclk);

	ret = set_sensor_gpio_function(sensor_gpio_func);
	if (ret < 0)
		goto err_set_sensor_gpio;

	bf3a03_attr.dvp.gpio = sensor_gpio_func;
	bf3a03_attr.max_dgain = 0;
	bf3a03_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP;
	sd = &sensor->sd;
	video = &sensor->video;
	sensor->video.attr = &bf3a03_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = V4L2_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &bf3a03_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("@@@@@@@ probe ok ------->bf3a03\n");

	return 0;

err_get_mclk:
	kfree(sensor);
err_set_sensor_gpio:
	clk_disable(sensor->mclk);
	clk_put(sensor->mclk);

	return -1;
}

static int bf3a03_remove(struct i2c_client *client)
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

static const struct i2c_device_id bf3a03_id[] = {
	{ "bf3a03", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bf3a03_id);

static struct i2c_driver bf3a03_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "bf3a03",
	},
	.probe		= bf3a03_probe,
	.remove		= bf3a03_remove,
	.id_table	= bf3a03_id,
};

static __init int init_bf3a03(void)
{
	int ret = 0;
	ret = private_driver_get_interface();
	if(ret){
		ISP_ERROR("Failed to init bf3a03 dirver.\n");
		return -1;
	}
	return private_i2c_add_driver(&bf3a03_driver);
}

static __exit void exit_bf3a03(void)
{
	private_i2c_del_driver(&bf3a03_driver);
}

module_init(init_bf3a03);
module_exit(exit_bf3a03);

MODULE_DESCRIPTION("A low-level driver for Gcoreinc bf3a03 sensors");
MODULE_LICENSE("GPL");
