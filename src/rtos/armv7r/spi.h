#ifndef _SPI_H_
#define _SPI_H_

u32 spi_read_id(void);
int spi_nor_erase(u32 addr);
int spi_nor_write(u32 addr, u32 data);
u32 spi_nor_read(u32 addr);





#define SPI_BLOCK_SIZE   0x8000
#define SPI_SAVELOG_SIZE 0x50000

#endif
