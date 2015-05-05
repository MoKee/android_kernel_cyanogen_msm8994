//*************************************************************************************************
// LGIT 검증 End Ver1.0 Sanyo LC898111A ES1 (2012.07.24)
//*************************************************************************************************

#include <linux/kernel.h>
#include "K7_Cmd.h"
#include "K7_ois.h"

//*************************************************************************************************
// 16bit - 16bit I2C Write 함수
//*************************************************************************************************

void RamWriteA( unsigned short RamAddr, unsigned short RamData ) 
{
	unsigned char RamAddrH = RamAddr >> 8;
	unsigned char RamAddrL = RamAddr & 0xFF; 
	
	unsigned char RamDataH = RamData >> 8;
	unsigned char RamDataL = RamData & 0xFF; 
	
	RamWrite( RamAddrH, RamAddrL, RamDataH, RamDataL ); 
}

void RamWrite( unsigned char RamAddrH, unsigned char RamAddrL, unsigned char RamDataH, unsigned char RamDataL ) // 8 bit 분리
{
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = RamAddrH << 8 | RamAddrL;
	reg_data = RamDataH << 8 | RamDataL;
	
	msm_ois_i2c_write(reg_addr, reg_data, 2);
	
}

//*************************************************************************************************
// 16bit - 16bit I2C Read 함수
//*************************************************************************************************

void RamReadA( unsigned short RamAddr, void * ReadData )
{
	unsigned char RamAddrH = RamAddr >> 8;
	unsigned char RamAddrL = RamAddr & 0xFF;

	RamRead( RamAddrH, RamAddrL, (unsigned short*)ReadData );
}

void RamRead( unsigned char RamAddrH, unsigned char RamAddrL, unsigned short * ReadData )
{
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = (RamAddrH << 8) | RamAddrL;

	msm_ois_i2c_read(reg_addr, &reg_data, 2);
	
	*ReadData = reg_data;
}


//*************************************************************************************************
// 16bit - 32bit I2C Write 함수
//*************************************************************************************************

void RamWrite32A( unsigned short RamAddr, unsigned long RamData )
{
	unsigned char RamAddrH = RamAddr >> 8;
	unsigned char RamAddrL = RamAddr & 0xFF;
	
	unsigned short RamDataH = RamData >> 16;
	unsigned short RamDataL = RamData & 0xFFFF;
	
	RamWrite32( RamAddrH, RamAddrL, RamDataH, RamDataL );
}


void RamWrite32( unsigned char RamAddrH, unsigned char RamAddrL, unsigned short RamDataH, unsigned short RamDataL )
{	
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = RamAddrH << 8 | RamAddrL;
	reg_data = RamDataH << 16 | RamDataL;
	
	msm_ois_i2c_write(reg_addr, reg_data, 4);
}

//*************************************************************************************************
// 16bit - 32bit I2C Read 함수
//*************************************************************************************************

void RamRead32A( unsigned short RamAddr, void * ReadData )
{
	unsigned char RamAddrH = RamAddr >> 8;
	unsigned char RamAddrL = RamAddr & 0xFF;

	RamRead32( RamAddrH, RamAddrL, (unsigned long*)ReadData );
}

void RamRead32( unsigned char RamAddrH, unsigned char RamAddrL, unsigned long * ReadData )
{
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = (RamAddrH << 8) | RamAddrL;

	msm_ois_i2c_read(reg_addr, &reg_data, 4);
	
	*ReadData = reg_data;
}

//*************************************************************************************************
// 16bit - 8bit I2C Write 함수
//*************************************************************************************************

void RegWriteA(unsigned short RegAddr, unsigned char RegData)
{
	unsigned char RegAddrH = RegAddr >> 8;
	unsigned char RegAddrL = RegAddr & 0xFF;

	RegWrite( RegAddrH, RegAddrL, RegData );
}

void RegWrite(unsigned char RegAddrH, unsigned char RegAddrL, unsigned char RegData)
{
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = RegAddrH << 8 | RegAddrL;
	reg_data = RegData;
	
	msm_ois_i2c_write(reg_addr, reg_data, 1);
}

//*************************************************************************************************
// 16bit - 8bit I2C Read 함수
//*************************************************************************************************

void RegReadA(unsigned short RegAddr, unsigned char *RegData)
{
	unsigned char RegAddrH = RegAddr >> 8;
	unsigned char RegAddrL = RegAddr & 0xFF;

	RegRead( RegAddrH, RegAddrL, RegData );
}

void RegRead(unsigned char RegAddrH, unsigned char RegAddrL, unsigned char *RegData)
{
	unsigned short reg_addr;
	unsigned int reg_data;

	reg_addr = (RegAddrH << 8) | RegAddrL;

	msm_ois_i2c_read(reg_addr, &reg_data, 1);
	
	*RegData = (unsigned char)reg_data;
}

//*************************************************************************************************
// EEPROM I2C함수 16bit-8bit
//*************************************************************************************************

void E2PRegReadA(unsigned short RegAddr, unsigned char * RegData)
{
	unsigned char RegAddrH = RegAddr >> 8;
	unsigned char RegAddrL = RegAddr & 0xFF;
	
	E2PRegRead( RegAddrH, RegAddrL, (unsigned char *) RegData );
}

void E2PRegRead( unsigned char RegAddrH, unsigned char RegAddrL, unsigned char *RegData)
{
	unsigned int addr;
	addr = (RegAddrH << 8) | RegAddrL;

	msm_ois_eeprom_read( addr, (unsigned char *)RegData );
}

void	E2pRed( unsigned short UsAdr, unsigned char UcLen, unsigned char *UcPtr )
{
	unsigned char UcCnt ;
	
	for( UcCnt = 0; UcCnt < UcLen;  UcCnt++ ) {

		E2PRegReadA( UsAdr + UcCnt , (unsigned char *)&UcPtr[ abs(( UcLen - 1 ) - UcCnt) ]) ;
	}
	
}


