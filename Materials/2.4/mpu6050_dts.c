#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "mpu6050reg.h"

#define MPU6050_CNT	1
#define MPU6050_NAME	"mpu6050"

struct mpu6050_dev {
	dev_t devid;				/* 设备号 	 */
	struct cdev cdev;			/* cdev 	*/
	struct class *class;		/* 类 		*/
	struct device *device;		/* 设备 	 */
	struct device_node	*nd; 	/* 设备节点 */
	int major;					/* 主设备号 */
	void *private_data;			/* 私有数据 		*/
	signed int gyro_x_adc;		/* 陀螺仪X轴原始值 	 */
	signed int gyro_y_adc;		/* 陀螺仪Y轴原始值		*/
	signed int gyro_z_adc;		/* 陀螺仪Z轴原始值 		*/
	signed int accel_x_adc;		/* 加速度计X轴原始值 	*/
	signed int accel_y_adc;		/* 加速度计Y轴原始值	*/
	signed int accel_z_adc;		/* 加速度计Z轴原始值 	*/
	signed int temp_adc;		/* 温度原始值 			*/
};

static struct mpu6050_dev mpu6050dev;

static int mpu6050_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int mpu6050_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &mpu6050dev; /* 设置私有数据 */
	return 0;
}

static s32 mpu6050_write_regs(struct mpu6050_dev *dev, u8 reg, u8 *buf, u8 len)
{
	u8 b[256];
	struct i2c_msg msg;
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	
	b[0] = reg;					/* 寄存器首地址 */
	memcpy(&b[1],buf,len);		/* 将要写入的数据拷贝到数组b里面 */
	
	msg.addr = client->addr;	/* mpu6050地址 */
	msg.flags = 0;				/* 标记为写数据 */
	
	msg.buf = b;				/* 要写入的数据缓冲区 */
	msg.len = len + 1;			/* 要写入的数据长度 */
	
	return i2c_transfer(client->adapter, &msg, 1);
}

static void mpu6050_write_onereg(struct mpu6050_dev *dev, u8 reg, u8 data)
{
	u8 buf = 0;
	buf = data;
	mpu6050_write_regs(dev, reg, &buf, 1);
}

static int mpu6050_read_regs(struct mpu6050_dev *dev, u8 reg, void *val, int len)
{
	int ret;
	struct i2c_msg msg[2];
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	
	/* msg[0]为发送要读取的首地址 */
	msg[0].addr = client->addr;			/*   */
	msg[0].flags = 0;					/* 标记为发送数据 */
	msg[0].buf = &reg;					/* 读取的首地址 */
	msg[0].len = 1;						/* reg长度*/
	
	/* msg[1]读取数据 */
	msg[1].addr = client->addr;			/*   */
	msg[1].flags = I2C_M_RD;			/* 标记为读取数据*/
	msg[1].buf = val;					/* 读取数据缓冲区 */
	msg[1].len = len;					/* 要读取的数据长度*/
	
	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret == 2) {
		ret = 0;
	} else {
		printk("i2c rd failed=%d reg=%06x len=%d\n",ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

static unsigned char mpu6050_read_onereg(struct mpu6050_dev *dev, u8 reg)
{
	u8 data = 0;
	
	mpu6050_read_regs(dev, reg, &data, 1);
	return data;
	
#if 0
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	return i2c_smbus_read_byte_data(client, reg);
#endif
}

void mpu6050_readdata(struct mpu6050_dev *dev)
{
	unsigned char data[14];
	mpu6050_read_regs(dev, MPU6050_RA_ACCEL_XOUT_H, data, 14);
	
	dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]); 
	dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]); 
	dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]); 
	dev->temp_adc    = (signed short)((data[6] << 8) | data[7]); 
	dev->gyro_x_adc  = (signed short)((data[8] << 8) | data[9]); 
	dev->gyro_y_adc  = (signed short)((data[10] << 8) | data[11]);
	dev->gyro_z_adc  = (signed short)((data[12] << 8) | data[13]);
}

static ssize_t mpu6050_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	signed int data[7];
	long err = 0;
	struct mpu6050_dev *dev = (struct mpu6050_dev *)filp->private_data;
	
	mpu6050_readdata(dev);
	data[0] = dev->gyro_x_adc;
	data[1] = dev->gyro_y_adc;
	data[2] = dev->gyro_z_adc;
	data[3] = dev->accel_x_adc;
	data[4] = dev->accel_y_adc;
	data[5] = dev->accel_z_adc;
	data[6] = dev->temp_adc;
	err = copy_to_user(buf, data, sizeof(data));
	return 0;
}

/* mpu6050操作函数 */
static const struct file_operations mpu6050_ops = {
	.owner = THIS_MODULE,
	.open = mpu6050_open,
	.read = mpu6050_read,
	.release = mpu6050_release,
};

void mpu6050_reginit(void)
{
	u8 value = 0;
	
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_PWR_MGMT_1, 0x80);
	mdelay(50);
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_PWR_MGMT_1, 0x01);
	mdelay(50);
	
	value = mpu6050_read_onereg(&mpu6050dev, MPU6050_RA_WHO_AM_I);
	printk("mpu6050 ID = %#X\r\n", value);	
	
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_SMPLRT_DIV, 0x00); 	/* 输出速率是内部采样率					*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_GYRO_CONFIG, 0x18); 	/* 陀螺仪±2000dps量程 				*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_ACCEL_CONFIG, 0x18); 	/* 加速度计±16G量程 					*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_CONFIG, 0x04); 		/* 陀螺仪低通滤波BW=20Hz 				*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_FF_THR, 0x04); /* 加速度计低通滤波BW=21.2Hz 			*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_PWR_MGMT_2, 0x00); 	/* 打开加速度计和陀螺仪所有轴 				*/
	// mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_PWR_MGMT_1, MPU6050_PWR1_CYCLE_BIT); 	/* 关闭低功耗 						*/
	mpu6050_write_onereg(&mpu6050dev, MPU6050_RA_FIFO_EN, 0x00);		/* 关闭FIFO						*/
}

static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	
	/* 1、构建设备号 */
	if (mpu6050dev.major) {
		mpu6050dev.devid = MKDEV(mpu6050dev.major, 0);
		register_chrdev_region(mpu6050dev.devid, MPU6050_CNT, MPU6050_NAME);
	} else {
		alloc_chrdev_region(&mpu6050dev.devid, 0, MPU6050_CNT, MPU6050_NAME);
		mpu6050dev.major = MAJOR(mpu6050dev.devid);
	}
	
	/* 2、注册设备 */
	cdev_init(&mpu6050dev.cdev, &mpu6050_ops);
	cdev_add(&mpu6050dev.cdev, mpu6050dev.devid, MPU6050_CNT);
	
	/* 3、创建类 */
	mpu6050dev.class = class_create(THIS_MODULE, MPU6050_NAME);
	if (IS_ERR(mpu6050dev.class)) {
		return PTR_ERR(mpu6050dev.class);
	}
	
	/* 4、创建设备 */
	mpu6050dev.device = device_create(mpu6050dev.class, NULL, mpu6050dev.devid, NULL, MPU6050_NAME);
	if (IS_ERR(mpu6050dev.device)) {
		return PTR_ERR(mpu6050dev.device);
	}
	
	mpu6050dev.private_data = client; /* 设置私有数据 */
	
	mpu6050_reginit();		
	return ret;
}

static int mpu6050_remove(struct i2c_client *client)
{
	/* 删除设备 */
	cdev_del(&mpu6050dev.cdev);
	unregister_chrdev_region(mpu6050dev.devid, MPU6050_CNT);
	
	/* 注销掉类和设备 */
	device_destroy(mpu6050dev.class, mpu6050dev.devid);
	class_destroy(mpu6050dev.class);
	return 0;
}

/* 传统匹配方式ID列表 */
static const struct i2c_device_id mpu6050_id[] = {
{"test,mpu6050", 0},  
{}
};

/* 设备树匹配列表 */
static const struct of_device_id mpu6050_of_match[] = {
{ .compatible = "test,mpu6050" },
{ /* Sentinel */ }
};

static struct i2c_driver mpu6050_driver = {
	.probe = mpu6050_probe,
	.remove = mpu6050_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "mpu6050",
		.of_match_table = mpu6050_of_match, 
	},
	.id_table = mpu6050_id,
};

static int __init mpu6050_init(void)
{
	int ret = 0;
	ret = i2c_add_driver(&mpu6050_driver);
	return ret;
}

static void __exit mpu6050_exit(void)
{
	i2c_del_driver(&mpu6050_driver);
}

module_init(mpu6050_init);
module_exit(mpu6050_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("chen");


