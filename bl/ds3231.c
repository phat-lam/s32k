/*
 * ds3231.c
 *
 *  Created on: 2017年10月18日
 *      Author: houxd
 */
#include "bl.h"
#include <stdint.h>
#include "S32K144.h" /* include peripheral declarations S32K144 */
#include <stdio.h>
#include "softi2c.h"
#include "convert.h"
#include "timelib.h"

#define DS3231_CHIP_ADDR 	0xD0    //器件地址

#define DS3231_SECOND       0x00    //秒
#define DS3231_MINUTE       0x01    //分
#define DS3231_HOUR         0x02    //时
#define DS3231_WEEK         0x03    //星期
#define DS3231_DAY          0x04    //日
#define DS3231_MONTH        0x05    //月
#define DS3231_YEAR         0x06    //年
//闹铃1
#define DS3231_SALARM1ECOND 0x07    //秒
#define DS3231_ALARM1MINUTE 0x08    //分
#define DS3231_ALARM1HOUR   0x09    //时
#define DS3231_ALARM1WEEK   0x0A    //星期/日
//闹铃2
#define DS3231_ALARM2MINUTE 0x0b    //分
#define DS3231_ALARM2HOUR   0x0c    //时
#define DS3231_ALARM2WEEK   0x0d    //星期/日
#define DS3231_CONTROL      0x0e    //控制寄存器
#define DS3231_STATUS       0x0f    //状态寄存器
#define BSY                 2       //忙
#define OSF                 7       //振荡器停止标志
#define DS3231_XTAL         0x10    //晶体老化寄存器
#define DS3231_TEMPERATUREH 0x11    //温度寄存器高字节(8位)
#define DS3231_TEMPERATUREL 0x12    //温度寄存器低字节(高2位)

void SET_SCL()	{PTB->PSOR |= 1 << 9;	   }
void CLR_SCL()	{PTB->PCOR |= 1 << 9;      }
void SET_SDA()	{PTB->PSOR |= 1 << 10;     }
void CLR_SDA()	{PTB->PCOR |= 1 << 10;     }
void SCL_OUT()	{PTB->PDDR |= 1 << 9;      }
void SCL_IN ()	{PTB->PDDR &= ~(1 << 9);   }
void SDA_OUT()	{PTB->PDDR |= 1 << 10;     }
void SDA_IN ()	{PTB->PDDR &= ~(1 << 10);  }
int  SDA_VAL()	{return PTB->PDIR & (1<<10);}

const struct SoftI2C_Ports I2C0 = {
	.SET_SCL 	=SET_SCL,
	.CLR_SCL 	=CLR_SCL,
	.SET_SDA 	=SET_SDA,
	.CLR_SDA 	=CLR_SDA,
	.SCL_OUT 	=SCL_OUT,
	.SCL_IN 	=SCL_IN,
	.SDA_OUT	=SDA_OUT,
	.SDA_IN		=SDA_IN,
	.SDA_VAL	=SDA_VAL,
	.HALF_CLK_CYCLE_US	=	10,
};
struct DS3231_Time{
	uint8_t sec;	//00-59
	uint8_t min;	//00-59
	uint8_t hour;	//00-23
	uint8_t wday;	//1~7
	uint8_t mday;	//1~32
	uint8_t mon;	//1~12
	uint8_t year;	//0~99
};
void ds3231_settime(const struct DS3231_Time *dsdat) {
	softi2c_send_data(&I2C0,DS3231_CHIP_ADDR,DS3231_SECOND,		7,(uint8_t*)dsdat);   //修改年
}
void ds3231_gettime(struct DS3231_Time *dsdat) {
	softi2c_read_data(&I2C0,DS3231_CHIP_ADDR,DS3231_SECOND,7,(uint8_t*)dsdat);
}
float ds3231_gettempr() {
	/*
	 *         /------tempr val--\
	 * [sign10 dat9...2] [dat1...0 unuse6...0]
	 * \-----byteh-----/ \------bytel--------/
	 */
	uint8_t tempr[2];
	softi2c_read_data(&I2C0,DS3231_CHIP_ADDR,DS3231_TEMPERATUREH,2,tempr);
	int sign = tempr[0] & 0b10000000;
	uint32_t t = tempr[0] & 0b01111111;
	t<<=2;
	t += (tempr[1]>>6)&0b11;
	float res = t * 0.25f;	//resolution
	if(sign)
		res = -res;
	return res;
}
void load_time()
{
	struct DS3231_Time dst;
	struct DS3231_Time dst2;
	ds3231_gettime(&dst2);	//local
	do{
		ds3231_gettime(&dst);
	}while(dst.sec==dst2.sec);
	struct tm ltm = {
		.tm_sec = bcd2int(dst.sec),
		.tm_min = bcd2int(dst.min),
		.tm_hour = bcd2int(dst.hour),
		.tm_wday = bcd2int(dst.wday)-1,
		.tm_mday = bcd2int(dst.mday),
		.tm_mon = bcd2int(dst.mon),
		.tm_year = (2000 + bcd2int(dst.year)) - 1900,
	};
	time_t now = mktime(&ltm) - __timezone(NULL);	//to utc
	time(&now);
}
void store_time()
{
	time_t t = time(NULL);		//utc
	struct tm ltm;
	localtime_s(&t,&ltm);		//to local
	struct DS3231_Time dst = {
		.sec = 	int2bcd(ltm.tm_sec),
		.min = 	int2bcd(ltm.tm_min),
		.hour = int2bcd(ltm.tm_hour),
		.wday = int2bcd(ltm.tm_wday+1),
		.mday = int2bcd(ltm.tm_mday),
		.mon = 	int2bcd(ltm.tm_mon),
		.year = int2bcd(ltm.tm_year%100),
	};
	ds3231_settime(&dst);
}
int main() {
	WDOG_disable();
	SOSC_init_8MHz(); /* Initialize system oscilator for 8 MHz xtal */
	SPLL_init_160MHz(); /* Initialize SPLL to 160 MHz with 8 MHz SOSC */
	NormalRUNmode_80MHz(); /* Init clocks: 80 MHz sysclk & core, 40 MHz bus, 20 MHz flash */
	NVIC_init_IRQs(); /* Enable desired interrupts and priorities */
	PORT_init(); /* Configure ports */

	softi2c_init_io(&I2C0);
	char asc[128];
	//2017/10/19 13:5:26
	time_t set = 1508389526UL;	//utc
	struct tm ltm;
	localtime_s(&set,&ltm);
	char *s = asctime_s(&ltm,asc,128);
	printf("%s\n",s);
	time(&set);
	store_time();

	float tempr = ds3231_gettempr();
	printf("temp=%f\n", tempr);
	load_time();
	while(1){
		time_t t = time(NULL);
		struct tm ltm;
		localtime_s(&t,&ltm);
		char *s = asctime_s(&ltm,asc,128);
		printf("%s\n",s);
		softi2c_test_delay_1s(1000000);
	}
	while (1)
		;
}
