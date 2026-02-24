#ifndef _SPI_H_
#define _SPI_H_

u32 spi_read_id(void);
int spi_flash_erase(u32 addr);
u32 spi_flash_read(u32 addr);
int spi_flash_write(u32 addr, u32 data);
u32 spi_read_spi_count(void);
void spi_reset_spi_count(void);
u32 spi_score_board(u8 u8rw, u8 u8ce, u16 u16pagenumber);



u32 spi_readl(int reg);


#endif

