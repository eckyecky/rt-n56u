#include <common.h>

#include <ralink_gpio.h>
#include "ralink_smi.h"

#define MDC_MDIO_CTRL0_REG		31
#define MDC_MDIO_START_REG		29
#define MDC_MDIO_CTRL1_REG		21
#define MDC_MDIO_ADDRESS_REG		23
#define MDC_MDIO_DATA_WRITE_REG		24
#define MDC_MDIO_DATA_READ_REG		25
#define MDC_MDIO_PREAMBLE_LEN		32

#define MDC_MDIO_START_OP		0xFFFF
#define MDC_MDIO_ADDR_OP		0x000E
#define MDC_MDIO_READ_OP		0x0001
#define MDC_MDIO_WRITE_OP		0x0003

#define SMI_ACK_RETRY_COUNT		5
#define SMI_CLK_DELAY_US		2 /* 2 microseconds */
#define SMI_SLAVE_ADDR			0xB8

static u32 g_gpio_sda = 1;
static u32 g_gpio_sck = 2;

static u32 g_phy_id = 0;

/////////////////////////////////////////////////////////////////////////////////

static void _gpio_direction_output(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODIR));
		tmp |= (1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODIR) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DIR));
		tmp |= (1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DIR) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DIR));
		tmp |= (1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DIR) = cpu_to_le32(tmp);
	}
}

static void _gpio_direction_input(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODIR));
		tmp &= ~(1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODIR) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DIR));
		tmp &= ~(1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DIR) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DIR));
		tmp &= ~(1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DIR) = cpu_to_le32(tmp);
	}
}

static void _gpio_set_value(u32 pin, u32 value)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODATA));
		if (value)
			tmp |= (1L << pin);
		else
			tmp &= ~(1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODATA) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DATA));
		if (value)
			tmp |= (1L << (pin-24));
		else
			tmp &= ~(1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DATA) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DATA));
		if (value)
			tmp |= (1L << (pin-40));
		else
			tmp &= ~(1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DATA) = cpu_to_le32(tmp);
	}
}

static u32 _gpio_get_value(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODATA));
		tmp = (tmp >> pin) & 1L;
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DATA));
		tmp = (tmp >> (pin-24)) & 1L;
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DATA));
		tmp = (tmp >> (pin-40)) & 1L;
	}

	return tmp;
}


/////////////////////////////////////////////////////////////////////////////////

static inline void _smi_clk_delay(void)
{
	udelay(SMI_CLK_DELAY_US);
}

static void _smi_start(void)
{
	/*
	 * Set GPIO pins to output mode, with initial state:
	 * SCK = 0, SDA = 1
	 */
	_gpio_direction_output(g_gpio_sck);
	_gpio_set_value(g_gpio_sck, 0);
	
	_gpio_direction_output(g_gpio_sda);
	_gpio_set_value(g_gpio_sda, 1);
	_smi_clk_delay();

	/* CLK 1: 0 -> 1, 1 -> 0 */
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();

	/* CLK 2: */
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 1);
}

static void _smi_stop(void)
{
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 0);
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);

	/* add a click */
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);

	/* set GPIO pins to input mode */
	_gpio_direction_input(g_gpio_sda);
	_gpio_direction_input(g_gpio_sck);
}

static void _smi_write_bits(u32 data, u32 len)
{
	for (; len > 0; len--) {
		_smi_clk_delay();

		/* prepare data */
		_gpio_set_value(g_gpio_sda, !!(data & ( 1 << (len - 1))));
		_smi_clk_delay();

		/* clocking */
		_gpio_set_value(g_gpio_sck, 1);
		_smi_clk_delay();
		_gpio_set_value(g_gpio_sck, 0);
	}
}

static void _smi_read_bits(u32 len, u32 *data)
{
	u32 u;
	_gpio_direction_input(g_gpio_sda);

	for (*data = 0; len > 0; len--) {
		_smi_clk_delay();

		/* clocking */
		_gpio_set_value(g_gpio_sck, 1);
		_smi_clk_delay();
		u = !!_gpio_get_value(g_gpio_sda);
		_gpio_set_value(g_gpio_sck, 0);

		*data |= (u << (len - 1));
	}

	_gpio_direction_output(g_gpio_sda);
	_gpio_set_value(g_gpio_sda, 0);
}

static int _smi_wait_for_ack(void)
{
	int retry_cnt;

	retry_cnt = 0;
	do {
		u32 ack = 0;

		_smi_read_bits(1, &ack);
		if (ack == 0)
			break;

		if (++retry_cnt > SMI_ACK_RETRY_COUNT) {
			return -1;
		}
	} while (1);

	return 0;
}

static int _smi_write_byte(u8 data)
{
	_smi_write_bits(data, 8);
	return _smi_wait_for_ack();
}

static void _smi_read_byte_x(u32 byte_index, u8 *data)
{
	u32 t;

	/* read data */
	_smi_read_bits(8, &t);
	*data = (t & 0xff);

	/* send an ACK */
	_smi_write_bits(byte_index, 1);
}

/////////////////////////////////////////////////////////////////////////////////

#if defined(MDC_MDIO_OPERATION)

extern void mii_mgr_init(void);
extern void ctrl_mdc_clock(int enable);
extern u32 mii_mgr_read(u32 phy_addr, u32 phy_register, u32 *read_data);
extern u32 mii_mgr_write(u32 phy_addr, u32 phy_register, u32 write_data);

#define MDC_MDIO_READ(a, b, c, d)		mii_mgr_read(b, c, d);
#define MDC_MDIO_WRITE(a, b, c, d)		mii_mgr_write(b, c, d);

void smi_init(rtk_uint32 gpio_clock, rtk_uint32 gpio_data)
{
	printf(" Switch control i/f: %s\n", "MDIO");
	
	mii_mgr_init();
}

int smi_read(rtk_uint32 addr, rtk_uint32 *data)
{
	/* We enable mdio gpio purpose register, and disable it when exit. */
	ctrl_mdc_clock(1);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write address control code to register 31 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write address to register 23 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_ADDRESS_REG, addr);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write read control code to register 21 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_READ_OP);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Read data from register 25 */
	MDC_MDIO_READ(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_DATA_READ_REG, data);

	ctrl_mdc_clock(0);

	return RT_ERR_OK;
}

int smi_write(rtk_uint32 addr, rtk_uint32 data)
{
	/* We enable mdio gpio purpose register, and disable it when exit. */
	ctrl_mdc_clock(1);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write address control code to register 31 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write address to register 23 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_ADDRESS_REG, addr);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write data to register 24 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_DATA_WRITE_REG, data);

	/* Write Start command to register 29 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_START_REG, MDC_MDIO_START_OP);

	/* Write data control code to register 21 */
	MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, g_phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_WRITE_OP);

	ctrl_mdc_clock(0);

	return RT_ERR_OK;
}

#else

void smi_init(rtk_uint32 gpio_clock, rtk_uint32 gpio_data)
{
	g_gpio_sck = gpio_clock;
	g_gpio_sda = gpio_data;

	printf(" Switch control i/f: %s (%d/%d)\n", "GPIO", gpio_clock, gpio_data);
}

int smi_read(rtk_uint32 addr, rtk_uint32 *data)
{
	u8 lo = 0;
	u8 hi = 0;
	int ret = RT_ERR_FAILED;

	_smi_start();

	/* send READ command */
	ret = _smi_write_byte(SMI_SLAVE_ADDR | 0x01);
	if (ret)
		goto out;

	/* set ADDR[7:0] */
	ret = _smi_write_byte(addr & 0xff);
	if (ret)
		goto out;

	/* set ADDR[15:8] */
	ret = _smi_write_byte(addr >> 8);
	if (ret)
		goto out;

	/* read DATA[7:0] */
	_smi_read_byte_x(0, &lo);
	
	/* read DATA[15:8] */
	_smi_read_byte_x(1, &hi);

	*data = ((u32) lo) | (((u32) hi) << 8);

	ret = RT_ERR_OK;

 out:
	_smi_stop();

	return ret;
}

int smi_write(rtk_uint32 addr, rtk_uint32 data)
{
	int ret = RT_ERR_FAILED;

	_smi_start();

	/* send WRITE command */
	ret = _smi_write_byte(SMI_SLAVE_ADDR);
	if (ret)
		goto out;

	/* set ADDR[7:0] */
	ret = _smi_write_byte(addr & 0xff);
	if (ret)
		goto out;

	/* set ADDR[15:8] */
	ret = _smi_write_byte(addr >> 8);
	if (ret)
		goto out;

	/* write DATA[7:0] */
	ret = _smi_write_byte(data & 0xff);
	if (ret)
		goto out;

	/* write DATA[15:8] */
	ret = _smi_write_byte(data >> 8);
	if (ret)
		goto out;

	ret = RT_ERR_OK;

 out:
	_smi_stop();

	return ret;
}

#endif
