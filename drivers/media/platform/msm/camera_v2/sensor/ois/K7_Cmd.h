#ifndef	K7_CMD_H_
#define	K7_CMD_H_

void msm_ois_i2c_write(unsigned short reg_addr, unsigned int reg_data, unsigned short data_size);
void msm_ois_i2c_read(unsigned short reg_addr, unsigned int *reg_data, unsigned short data_size);
void msm_ois_eeprom_read(unsigned int RegAddr, unsigned char * RegData);
void msm_ois_wait_time_ms(unsigned short time_ms);

void RamWrite32A(unsigned short, unsigned long);
void RamWrite32(unsigned char, unsigned char, unsigned short, unsigned short);

void RamWriteA(unsigned short, unsigned short);
void RamWrite(unsigned char, unsigned char, unsigned char, unsigned char);
	
void RegWriteA(unsigned short, unsigned char);
void RegWrite(unsigned char, unsigned char, unsigned char);
	
void RamRead32A( unsigned short, void * ) ;
void RamRead32( unsigned char, unsigned char, unsigned long * ) ;
	
void RamReadA( unsigned short, void * ) ;
void RamRead( unsigned char, unsigned char, unsigned short * ) ;

void RegReadA(unsigned short, unsigned char *);
void RegRead(unsigned char, unsigned char, unsigned char *);
	
void E2PRegReadA(unsigned short,  unsigned char *);
void E2PRegRead(unsigned char, unsigned char, unsigned char *);

void E2pRed( unsigned short, unsigned char, unsigned char * ) ;	// E2P ROM Data Read

#endif
/* _CMD_H_ */