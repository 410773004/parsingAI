#include "types.h"
#include "io.h"
#include "misc.h"
#include "spi_register.h"
#include "sect.h"
#include "spi.h"
#include "stdio.h"
#define __FILEID__ spi
#include "trace.h"
#define SPI_ID        0x3525C2

//#define SPI_ID        0x001360EF
#if 0

#define SPI_CTRL_BASE 0xC0051000
#define SPI_TX_XFER_CLEAN spi_tx_data(0x0);

enum spi_cmd_t {
    SPI_PP     = 0x02, //page program sequence
    SPI_READ   = 0x03, //read data bytes sequence
    SPI_RDSR   = 0x05, //read status
    SPI_WREN   = 0x06, //write enable
    SPI_RDSCUR = 0x2B,
    SPI_BE32K  = 0x52, //block erase 32kB
    SPI_BE4K   = 0x20, //Sector erase 4kB
    SPI_CE     = 0x60, //chip erase
    SPI_RDID   = 0x9F, //read ID

};
#endif
enum spi_ctrl_t {
    SPI_READ_TXFR  = 0x0,
    SPI_WRITE_TXFR = 0x1,
};
enum spi_tpsel_t {
    SPI_INST   = 0x0,
    SPI_STATUS = 0x1,
    SPI_DATA   = 0x2,
};
static void inline spi_writel(u32 data, int reg)
{
    writel(data, (void *)(SPI_CTRL_BASE + reg));
}
//static u32 inline spi_readl(int reg)
ddr_code u32 inline spi_readl(int reg)

{
    return readl((void *)(SPI_CTRL_BASE + reg));
}

/* reset ctrl it self */
ddr_code static void spi_reset(void) {
    spi_control_register_1_t r1;

    r1.all = spi_readl(SPI_CONTROL_REGISTER_1);
    r1.b.spi_sw_rst = 1;
    spi_writel(r1.all, SPI_CONTROL_REGISTER_1);

    r1.all = spi_readl(SPI_CONTROL_REGISTER_1);
    r1.b.spi_sw_rst = 0;
    spi_writel(r1.all, SPI_CONTROL_REGISTER_1);
}

ddr_code static void spi_setup(void) {
    spi_status_register_2_t s2;

    s2.all = spi_readl(SPI_STATUS_REGISTER_2);
    s2.b.spi_sck_sel = 1;// TODO
    s2.b.spi_br_div  = 7;// Function: 800MHz /(br_div + 1), therefore br_div equal 7 be SPI clock is 100MHz.
    s2.b.spi_tran_db_len = 3;
    s2.b.spi_int_en  = 0;
    spi_writel(s2.all, SPI_STATUS_REGISTER_2);
}

ddr_code static void spi_wait_xfer(void) {
    spi_status_register_t sts;
    do {
        sts.all = spi_readl(SPI_STATUS_REGISTER);
    } while (sts.b.spi_fsm_st != 0);
}

ddr_code static void spi_start(int write, int len, int sts) {
    spi_status_register_2_t s2;

    s2.all = spi_readl(SPI_STATUS_REGISTER_2);
    s2.b.spi_rw_ctrl = write;
    s2.b.spi_tran_db_len = len; //The value means the byte length per SPI transfer 3=4BYTE
    s2.b.spi_tran_tp_sel = sts;
    spi_writel(s2.all, SPI_STATUS_REGISTER_2);

    spi_control_register_1_t c1;
    c1.all = spi_readl(SPI_CONTROL_REGISTER_1),
    c1.b.spi_tran_st = 1;
    spi_writel(c1.all, SPI_CONTROL_REGISTER_1);

    c1.all = spi_readl(SPI_CONTROL_REGISTER_1),
    c1.b.spi_tran_st = 0;
    spi_writel(c1.all, SPI_CONTROL_REGISTER_1);

    spi_wait_xfer();
}

ddr_code static void spi_cmd_addr(u8 cmd, u32 addr) {
    spi_command_register_t c;
    c.all = spi_readl(SPI_COMMAND_REGISTER);
    c.b.spi_inst = cmd;
    c.b.spi_tx_addr = addr;
    spi_writel(c.all, SPI_COMMAND_REGISTER);
}

ddr_code static void spi_cmd(u8 cmd) {
    spi_command_register_t c;
    c.all = spi_readl(SPI_COMMAND_REGISTER);
    c.b.spi_inst = cmd;
    spi_writel(c.all, SPI_COMMAND_REGISTER);
}

ddr_code static void spi_tx_data(u32 addr)
{
    spi_writel(addr, SPI_TX_DATA_REGISTER);
}

ddr_code static u32 spi_read_cmd(void) {
    spi_wait_xfer();
    spi_rx_data_register_t rx;
    rx.all = spi_readl(SPI_RX_DATA_REGISTER);
    return rx.all;
}

ddr_code static u32 spi_rdsr(void) {
    spi_cmd(SPI_RDSR);
    spi_start(SPI_READ_TXFR, 0, SPI_STATUS);

    return spi_read_cmd();
}

ddr_code static u32 spi_rdscur(void)
{
    spi_cmd(SPI_RDSCUR);
    spi_start(SPI_READ_TXFR, 0, SPI_STATUS);

    return spi_read_cmd();
}

ddr_code int spi_flash_init(void) {
    spi_reset();
    spi_setup();

    /* send read ID to flash */
    spi_cmd(SPI_RDID);
    spi_start(SPI_READ_TXFR, 2, SPI_STATUS);
    u32 data = spi_read_cmd();
    //spi_core_trace(LOG_ERR, 0, "Flash id: 0x%x\n", data & 0xFFFFFF);
    if ((data & 0xFFFFFF) == SPI_ID)
        return 0;

    return -1;
}

ddr_code int spi_chip_erase() {
    u32 sts;

    /* write enable */
    spi_cmd(SPI_WREN);
    spi_start(SPI_WRITE_TXFR, 0, SPI_INST);
    sts = spi_rdsr();
    if ((sts & 0x2) == 0)
        return -1;

    spi_cmd(SPI_CE);
    spi_start(SPI_WRITE_TXFR, 0, SPI_INST);

    do {
        sts = spi_rdsr();
    } while ((sts & 0x1) != 0);

    return 0;
}


ddr_code int spi_nor_erase(u32 addr) 
{

    u32 sts;
redo:
	//spi_core_trace(LOG_ERR, 0, "spi_flash_erase\n");
    // write enable 
    spi_cmd(SPI_WREN);
    spi_start(SPI_WRITE_TXFR, 0, SPI_INST);
    sts = spi_rdsr()&0xFF;
	//spi_core_trace(LOG_ERR, 0, "sts_1 = %x\n",sts);
    if ((sts & 0x2) == 0){
	    goto redo;
    }
    // BE32K
    spi_cmd(SPI_BE32K);

    u32 data;
    data = ((addr & 0x00ff0000)>>16) +
           ((addr & 0x0000ff00))     +
           ((addr & 0x000000ff)<<16);
	//spi_core_trace(LOG_ERR, 0, "data = %d\n",data);//800080ff
    spi_tx_data(data);
    spi_start(SPI_WRITE_TXFR, 2, SPI_STATUS);
	SPI_TX_XFER_CLEAN

    do {
        sts = spi_rdsr()&0xFF;
    } while ((sts & 0x1) != 0);

    sts = spi_rdscur()&0xFF;
	//spi_core_trace(LOG_ERR, 0, "sts_2 = %d\n",sts);
    if (sts & (1 << 6)){
	    return -1;
    }
    return 0;
}

ddr_code int spi_nor_write(u32 addr, u32 data) 
{
    u32 sts;	
redo:
	//spi_core_trace(LOG_ERR, 0, "spi_flash_write\n");
    // write enable
    spi_cmd(SPI_WREN);
    spi_start(SPI_WRITE_TXFR, 0, SPI_INST);
    sts = spi_rdsr()&0xFF;
	//spi_core_trace(LOG_ERR, 0, "sts_1 = %d\n",sts);
    if ((sts & 0x2) == 0){
	    goto redo;
    }
    spi_cmd_addr(SPI_PP, addr);
    spi_tx_data(data);
    spi_start(SPI_WRITE_TXFR, 3, SPI_DATA);
    SPI_TX_XFER_CLEAN
    do {
        sts = spi_rdsr()&0xFF;
    } while ((sts & 0x1) != 0);
	
    sts = spi_rdscur()&0xFF;
    if (sts & (1 << 5)){
	    return -1;
    }
    return 0;
}

ddr_code u32 spi_nor_read(u32 addr){//, u32 *data) {

    spi_cmd_addr(SPI_READ, addr);
    spi_start(SPI_READ_TXFR, 3, SPI_DATA);

    return spi_readl(SPI_RX_DATA_REGISTER);
}
ddr_code u32 spi_nor_read_1B(u32 addr){//, u32 *data) {

    spi_cmd_addr(SPI_READ, addr);
    spi_start(SPI_READ_TXFR, 0, SPI_DATA);

    return spi_readl(SPI_RX_DATA_REGISTER);
}

ddr_code u32 spi_read_id(void) {
    spi_reset();
    spi_setup();

    /* send read ID to flash */
    spi_cmd(SPI_RDID);
    spi_start(SPI_READ_TXFR, 2, SPI_STATUS);
    u32 data = (spi_read_cmd() & 0xFFFFFF);
	
    return data;

}

ddr_code u32 spi_read_spi_count(void)
{
	return spi_flash_read(0x0);
}

ddr_code void spi_reset_spi_count(void)
{

	if(spi_flash_erase(0x0)!=0){
		printk("Spi Counter Erase Fail.\n");
	}

	if(spi_flash_write(0x0, 0x0)!=0){
		printk("Spi Counter Write Fail.\n");
	}
}

ddr_code u32 spi_score_board(u8 u8rw, u8 u8ce, u16 u16pagenumber)
{
	spi_score_board_t sb;
	if(u8rw==0){
		printk("Score Board Read.\n");
		sb.all = spi_flash_read(SCORE_BOARD_ADDR);
	    return sb.all;
	}
	else{
		printk("Score Board Write.\n");
		sb.b.fsm = u8rw;
		sb.b.ce = u8ce;
	    sb.b.pagenumber = u16pagenumber;
		if(spi_flash_erase(SCORE_BOARD_ADDR) != 0){
			printk("Erase Fail.\n");
			return 0xFFFFFFFF;
		}
		if(spi_flash_write(SCORE_BOARD_ADDR, sb.all) != 0){
			printk("Write Fail.\n");
			return 0xFFFFFFFF;
		}
		return 0x0;
	}
}

