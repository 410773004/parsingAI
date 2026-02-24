//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

//=============================================================================
//
/*! \file
 * @brief for internal thermal sensor
 *
 * \addtogroup rtos
 * \defgroup thermal
 * \ingroup rtos
 */
 //=============================================================================
#include "types.h"
#include "io.h"
#include "mod.h"
#include "smb_registers.h"
#include "sect.h"
#include "stdio.h"
#include "console.h"
#include "string.h"
#include "spin_lock.h"
#include "plp.h"



/*! \cond PRIVATE */
#define __FILEID__ regu
#include "trace.h"
/*! \endcond */

#define IIC_BASE 0xC0053000		///< smb control register base address

#define M2_REGULATOR_NAND_CORE	0x6
#define M2_REGULATOR_DDR	0x7
#define M2_REGULATOR_NAND_IO	0x8
#define M2_REGULATOR_DDR_VPP	0xB
extern bool I2C_read_lock;
extern esr_err_flags_t esr_err_flags;
extern u8 Detect_FLAG;

typedef enum {
	SMB_OK,
	SMB_FAIL,
	SMB_TMO
} smb_sts_t;
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
static fast_data smb_registers_regs_t * smb =
		(smb_registers_regs_t*)(IIC_BASE);	///< smb control register


static inline void lock_regulator(void)
{
#if defined(MPC)
	spin_lock_take(SPIN_LOCK_PR_ACCESS, 0, true);
#endif
}

static inline void unlock_regulator(void)
{
#if defined(MPC)
	spin_lock_release(SPIN_LOCK_PR_ACCESS);
#endif
}

/*
 *	read one from IIC bus
 */
fast_code u32 pr_read(u8 cmd_code, u8 *value, u8 type)
{
	u32 data0;
	smb_init_0_t smb_init_0;
	smb_control_register_t smb_control_register;
	u32 cnt = 0x7FFFFF;
	u32 sts =0 ;

	lock_regulator();

	/* set init rd and wr address */
	smb_init_0.all = 0;
#if defined(M2_2A)
	smb_init_0.b.smb_init_addr_w = 0x60;
	smb_init_0.b.smb_init_addr_r = 0x61;
#else
   	if(type == 1)
   	{
	  	smb_init_0.b.smb_init_addr_w = 0x90;  // TPS62864
		smb_init_0.b.smb_init_addr_r = 0x91;  // TPS62864
   	}
	else
	{
		smb_init_0.b.smb_init_addr_w = 0xC0; // SY8827 target address,0xD2: 1101 0010, 0x90: 1001 000x
		smb_init_0.b.smb_init_addr_r = 0xC1; // SY8827 target address 0xD3: 1101 0011, 0x91: 1001 0001
	}

#endif
	smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
	writel(smb_init_0.all, &smb->smb_init_0);

	/* set smb enable and read word start */
	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x0B;//master mode, read, 0x6: master, write
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb->smb_control_register);

	// temp printing before sensor issue clarified
	while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
		if (++cnt == 0x7FFFFFF) {
			cnt = 0;
			sts = readl(&smb->smb_intr_sts);
			rtos_smb_trace(LOG_ALW, 0x590b, " [pr_read error] intr_sts:%x", sts);
			break;
		}
	}
	smb->smb_intr_sts.all = readl(&smb->smb_intr_sts);
	if (smb->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK){
		rtos_smb_trace(LOG_ALW, 0x075d, " [pr_read error] intr_sts:%x", sts);
		writel(sts, &smb->smb_intr_sts);
		goto error;
		}
	smb->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
	writel(smb->smb_intr_sts.all, &smb->smb_intr_sts);
	data0 = readl(&smb->smb_pasv_0);
	*value = data0 >> 24;
	unlock_regulator();
	return 0;
error:
	unlock_regulator();
	return 0xFFFFFFFF;
}
/*
 *	write one byte to device through IIC bus
 */
ddr_code u32 pr_write(u8 cmd_code, u16 index, u8 type) // 20210224 Jamie slow_code -> ddr_code
{
	smb_init_0_t smb_init_0;
	smb_init_wr_1_t smb_wr_data;
	smb_control_register_t smb_control_register;
	u32 cnt = 0x7FFFFF;
	u32 sts =0;
	u16 data;

	lock_regulator();

	/* set init rd and wr address */
	smb_init_0.all = 0;
#if defined(M2_2A)
	smb_init_0.b.smb_init_addr_w = 0x60;
	smb_init_0.b.smb_init_addr_r = 0x61;
#else
    // Corepower operation limit on range from 0.88v to 0.72v.
    if(index>88){
		index=88;
    }
	else if(index<=72){
		index=72;
	}

   	if(type == 1)
   	{
	  	smb_init_0.b.smb_init_addr_w = 0x90;  // TPS62864
		smb_init_0.b.smb_init_addr_r = 0x91;  // TPS62864
		data = (index<<1)-80;
		// Deacription: Jimmitt 2020/12/10
		// This function is regulator settig for mVoltage transfer to TI value.
		// Example: index = 80 be 800mv, 75 be 750mv
   	}
	else
	{
		smb_init_0.b.smb_init_addr_w = 0xC0; // SY8827 target address,0xD2: 1101 0010, 0x90: 1001 000x
		smb_init_0.b.smb_init_addr_r = 0xC1; // SY8827 target address 0xD3: 1101 0011, 0x91: 1001 0001
		data = ((index<<2)/5)+80;
		// Deacription: Jimmitt 2020/12/10
		// This function is regulator settig for mVoltage transfer to SY value.
		// Example: index = 80 be 800mv, 75 be 750mv
	}
#endif
	smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
	writel(smb_init_0.all, &smb->smb_init_0);

	/* set the data*/
	smb_wr_data.b.smb_init_wr_data01 = data;
	writel(smb_wr_data.all, &smb->smb_init_wr_1);

	/* set smb enable and write word start */
	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x0A;//master mode, read, 0x6: master, write
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb->smb_control_register);

	if (type == 1)
	{
	    extern void mdelay(u32 ms);
	    mdelay(5);
	}

	while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
		if (++cnt == 0x7FFFFFF) {
			cnt = 0;
			sts = readl(&smb->smb_intr_sts);
			rtos_smb_trace(LOG_ALW, 0xdbdc, " [pr_write error] intr_sts:%x", sts);
			break;
		}
	}
	smb->smb_intr_sts.all = readl(&smb->smb_intr_sts);
	// when ACK_DONE_ERR, return hardcode value
	if (smb->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK){
		rtos_smb_trace(LOG_ALW, 0x2659, " [pr_write error] intr_sts:%x", sts);
		writel(sts, &smb->smb_intr_sts);
		goto error;
		}
	smb->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
	writel(smb->smb_intr_sts.all, &smb->smb_intr_sts);
	unlock_regulator();
	return 0x0;
error:
	unlock_regulator();
	return 0xFFFFFFFF;
}

fast_code void poweroff_nand(void)
{
#if defined(M2_0305)
	u8 data1;
	u8 data2;
	while (pr_read(M2_REGULATOR_NAND_IO, &data1, 0))
		;

	if (data1 & 0x80)
		pr_write(M2_REGULATOR_NAND_IO, data1 & 0x7F);

	while (pr_read(M2_REGULATOR_NAND_CORE, &data2, 0))
		;

	if (data2 & 0x80)
		pr_write(M2_REGULATOR_NAND_CORE, data2 & 0x7F);
	rtos_mmgr_trace(LOG_ERR, 0x1bc3, "NAND power is turned off: data = %02x %02x\n", data1, data2);
#endif
}

fast_code void poweron_nand(void)
{
#if defined(M2_0305)
	u8 data1;
	u8 data2;
	while (pr_read(M2_REGULATOR_NAND_IO, &data1, 0))
		;

	if ((data1 & 0x80) == 0)
		pr_write(M2_REGULATOR_NAND_IO, data1 | 0x80, 0);

	while (pr_read(M2_REGULATOR_NAND_CORE, &data2, 0))
		;

	if ((data2 & 0x80) == 0)
		pr_write(M2_REGULATOR_NAND_CORE, data2 | 0x80, 0);

	rtos_mmgr_trace(LOG_ERR, 0x6119, "NAND power is turned on: data = %02x %02x\n", data1, data2);
#endif
}

/*
	turn ddr power on
*/
fast_code void poweron_ddr(void)
{
#if defined(M2_0305)
	u8 data1;
	u8 data2;

	while (pr_read(M2_REGULATOR_DDR, &data1, 0))
		;
	if ((data1 & 0x80) == 0)
		pr_write(M2_REGULATOR_DDR, data1 | 0x80, 0);

	while (pr_read(M2_REGULATOR_DDR_VPP, &data2, 0))
			;
	if ((data2 & 0x80) == 0)
		pr_write(M2_REGULATOR_DDR_VPP, data2 | 0x80, 0);

	rtos_mmgr_trace(LOG_ERR, 0x26ee, "DDR power is turned on: data = %02x %02x\n", data1, data2);
#endif
}

/*
 * turn ddr power off
 */
fast_code void poweroff_ddr(void)
{
#if defined(M2_0305)
	u8 data1;
	u8 data2;

	while (pr_read(M2_REGULATOR_DDR, &data1, 0))
		;
	if (data1 & 0x80)
		pr_write(M2_REGULATOR_DDR, data1 & 0x7F);

	while (pr_read(M2_REGULATOR_DDR_VPP, &data2, 0))
		;
	if (data2 & 0x80)
		pr_write(M2_REGULATOR_DDR_VPP, data2 & 0x7F);

	rtos_mmgr_trace(LOG_ERR, 0xac0d, "DDR power is turned off: data = %02x %02x\n", data1, data2);
#endif
}

#if CPU_ID == 1
static ddr_code int power_console(int argc, char* argv[])
{
	u32 ret1, ret2;
	u8 cmd_code;
	if (argc < 3) {
		rtos_mmgr_trace(LOG_ERR, 0x72d5, "\nusage: regulator g reg_address or regulator s reg_addr value\n");
		return 0;
	}
	if (argv[1][0] == 'g' || argv[1][0] == 'G') { // get register value
		u8 value1,value2;
		cmd_code = atoi(argv[2]);

		rtos_mmgr_trace(LOG_ERR, 0x1c9b, "\nargc = %d, argv[2] = %s, cmd_code = %02x\n", argc, argv[2], cmd_code);
		ret1 = pr_read(cmd_code, &value1, 0);
		ret2 = pr_read(cmd_code, &value2, 1);
		rtos_mmgr_trace(LOG_ERR, 0xfa41, "\ncontrol the power regulator set %d, to 0x%08x,0x%08x, ret = %08x, ret2 = %08x\n", cmd_code, value1, value2, ret1, ret2);

	}
	else if (argv[1][0] == 's' || argv[1][0] == 'S') {
			u32 value;
			cmd_code = atoi(argv[2]);
			value = strtol(argv[3], (void *)0, 0);
			rtos_mmgr_trace(LOG_ERR, 0x7c07, "\nargc = %d, argv[2] = %s, argv[3]=%s, reg = %02x, value=%08x\n",
					argc, argv[2], argv[3], cmd_code, value);
			ret1 = pr_write(cmd_code, value, 0);
			ret2 = pr_write(cmd_code, value, 1);
			rtos_mmgr_trace(LOG_ERR, 0xc2c9, "\ncontrol the power regulator set %d, to 0x%08x, ret = %08x, ret2 = %08x\n", cmd_code, value, ret1, ret2);
	}
	else {
		rtos_mmgr_trace(LOG_ERR, 0x1646, "\nusage: regulator g reg_address or regulator s reg_addr value\n");
	}
	return 0;
}

static DEFINE_UART_CMD(power_ctl, "regulator",
	"pctrl",
	"power regulator control",
	2, 3, power_console);



slow_code u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value)
{
    if(I2C_read_lock == true)
        return 0xFFFFFFFE;
    I2C_read_lock = true;
    u32 data0;
    smb_init_0_t smb_init_0;
    smb_control_register_t smb_control_register;
    u32 cnt = 0x7FFFFF;
    u32 sts;

	sts = readl(&smb->smb_intr_sts);
	//rtos_smb_trace(LOG_ALW, 0x4a01, " [intr_sts before I2C read]: %x", sts);

    lock_regulator();

    /* set init rd and wr address */
    smb_init_0.all = 0;
    smb_init_0.b.smb_init_addr_w = slaveID;
    smb_init_0.b.smb_init_addr_r = slaveID + 1;

    smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
    writel(smb_init_0.all, &smb->smb_init_0);

    /* set smb enable and read word start */
    smb_control_register.all = 0;
    smb_control_register.b.smb_en = 0x01;
    smb_control_register.b.smb_tran_tp_sel = 0x0B;//master mode, read, 0x6: master, write
    smb_control_register.b.smb_mas_tran_st = 0x01;
    smb_control_register.b.smb_slv_timer_opt = 0x01;
    writel(smb_control_register.all, &smb->smb_control_register);

    // temp printing before sensor issue clarified
    while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
        if (++cnt == 0x7FFFFFF) {
          cnt = 0;
          sts = readl(&smb->smb_intr_sts);
          rtos_smb_trace(LOG_ALW, 0xa272, " [I2C read error]intr_sts %x", sts);
		  break;
        }
    }
    sts = readl(&smb->smb_intr_sts);
    writel(sts, &smb->smb_intr_sts);
    //rtos_smb_trace(LOG_INFO, 0, "1tmp %x smb->smb_intr_sts %x", sts, smb->smb_intr_sts.all);
    if (sts& WR_ACK_DONE_ERR_STS_MASK){
		rtos_smb_trace(LOG_ALW, 0x8211, " [I2C read error] intr_sts:%x slaveID 0x%x cmd_code 0x%x", sts, slaveID, cmd_code);
		writel(sts, &smb->smb_intr_sts);
        goto error;
    }
    //rtos_smb_trace(LOG_INFO, 0, "2tmp %x smb->smb_intr_sts %x", sts, smb->smb_intr_sts.all);
    data0 = readl(&smb->smb_pasv_0);
    *value = data0 >> 24;
    unlock_regulator();
    I2C_read_lock = false;
	Detect_FLAG = 0;
    return 0;
error:
    unlock_regulator();
    //rtos_smb_trace(LOG_ERR, 0, "I2C_read error sts %x", sts);
    I2C_read_lock = false;
	Detect_FLAG =0;
    return 0xFFFFFFFF;



#if 0
  u32 data0;
  smb_init_0_t smb_init_0;

  smb_control_register_t smb_control_register;
  u32 cnt = 0x7FFFFF;
  u32 sts;

  /* set init rd and wr address */
  smb_init_0.all = 0;
  //rtos_mmgr_trace(LOG_ERR, 0, "I2C_read initttttttttttttttttttttt\n");
  //smb_init_0.b.smb_init_addr_w = 0xD2; //target address,0xD2: 1101 0010, 0x90: 1001 000x                                      //Commented out to test PLP pr_read
  //smb_init_0.b.smb_init_addr_r = 0xD3; //target address 0xD3: 1101 0011, 0x91: 1001 0001
  smb_init_0.b.smb_init_addr_w = slaveID;
  smb_init_0.b.smb_init_addr_r = slaveID + 1;
  //rtos_mmgr_trace(LOG_ERR, 0, "smb_init_addr_r is : %x, slave ID + 1 is : %x cmdcode:%x\n", smb_init_0.b.smb_init_addr_w, slaveID + 1, cmd_code);                     //Shane Debug2
  smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
  writel(smb_init_0.all, &smb->smb_init_0);
  writel(0x05550562, &smb->smb_timer_0);
  writel(0x30551062, &smb->smb_timer_1);
  writel(0x4F112E2E, &smb->smb_timer_2);
  writel(0x112E2E05, &smb->smb_timer_3);
  writel(0x2E2E054F, &smb->smb_timer_4);
  writel(0x5B054F11, &smb->smb_timer_5);

  smb->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
  writel(smb->smb_intr_sts.all, &smb->smb_intr_sts);

  /* set smb enable and read word start */
  smb_control_register.all = 0;
  smb_control_register.b.smb_en = 0x01;
  //smb_control_register.b.smb_tran_i2c_en = 1;
  smb_control_register.b.smb_tran_tp_sel = 0x0B;//master mode, read, 0x6: master, write
  smb_control_register.b.smb_mas_tran_st = 0x01;
  smb_control_register.b.smb_slv_timer_opt = 0x01;
  writel(smb_control_register.all, &smb->smb_control_register);

  // temp printing before sensor issue clarified
  while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
      if (++cnt == 0x7FFFFFF) {
          cnt = 0;
          sts = readl(&smb->smb_intr_sts);
          rtos_smb_trace(LOG_ALW, 0xaa22, "smb_intr_sts %x", sts);
      }
  }
  smb->smb_intr_sts.all = readl(&smb->smb_intr_sts);
  if (smb->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK)
      goto error;
  smb->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
  writel(smb->smb_intr_sts.all, &smb->smb_intr_sts);
  data0 = readl(&smb->smb_pasv_0);
  *value = data0 >> 24;
    //rtos_mmgr_trace(LOG_ERR, 0, "pr_read data 0 / value / *value is: %x / %x /%x \n", data0, value, *value);                     //Shane Debug2
  return 0;
error:
    rtos_mmgr_trace(LOG_ERR, 0xb266, "pr_read error\n");                                                                             //Shane Debug2
  return 0xFFFFFFFF;
#endif
}




slow_code u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value)
{
#if 1
    if(I2C_read_lock == true)
            return 0xFFFFFFFE;
    I2C_read_lock = true;

    smb_init_0_t smb_init_0;
	smb_init_wr_1_t smb_wr_data;
	smb_control_register_t smb_control_register;
	u32 cnt = 0x7FFFFF;
	u32 sts;

	sts = readl(&smb->smb_intr_sts);
	//rtos_smb_trace(LOG_ALW, 0x14ef, " [intr_sts before I2C write]: %x", sts);

	lock_regulator();

	/* set init rd and wr address */
	smb_init_0.all = 0;
    smb_init_0.b.smb_init_addr_w = slaveID; //target address,0xD2: 1101 0010, 0x90: 1001 000x
    smb_init_0.b.smb_init_addr_r = slaveID + 1; //target address 0xD3: 1101 0011, 0x91: 1001 0001

	smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
	writel(smb_init_0.all, &smb->smb_init_0);

	/* set the data*/
	smb_wr_data.b.smb_init_wr_data01 = value;
	writel(smb_wr_data.all, &smb->smb_init_wr_1);

	/* set smb enable and write word start */
	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x0A;//master mode, read, 0x6: master, write
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb->smb_control_register);

	while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
		if (++cnt == 0x7FFFFFF) {
			cnt = 0;
			sts = readl(&smb->smb_intr_sts);
			rtos_smb_trace(LOG_ALW, 0x3d26, " [I2C write error] intr_sts:%x", sts);
			break;
		}
	}
	sts = readl(&smb->smb_intr_sts);
    writel(sts, &smb->smb_intr_sts);
	// when ACK_DONE_ERR, return hardcode value
	if (sts & WR_ACK_DONE_ERR_STS_MASK){
		rtos_smb_trace(LOG_ALW, 0x17f6, " [I2C write error] intr_sts:%x slaveID 0x%x cmd_code 0x%x", sts, slaveID, cmd_code);
		writel(sts, &smb->smb_intr_sts);
		goto error;
		}
	unlock_regulator();
    I2C_read_lock = false;
	return 0x0;
error:
	unlock_regulator();
    I2C_read_lock = false;
	return 0xFFFFFFFF;
#endif

#if 0
  smb_init_0_t smb_init_0;
  smb_init_wr_1_t smb_wr_data;
  smb_control_register_t smb_control_register;
  u32 cnt = 0x7FFFFF;
  u32 sts;
  //u8 next_byte;

  //while (I2C_read(slaveID, cmd_code + 1, &next_byte)) //workaround to support byte write
  /* set init rd and wr address */
  smb_init_0.all = 0;
  smb_init_0.b.smb_init_addr_w = slaveID; //target address,0xD2: 1101 0010, 0x90: 1001 000x
  smb_init_0.b.smb_init_addr_r = slaveID + 1; //target address 0xD3: 1101 0011, 0x91: 1001 0001
  //rtos_mmgr_trace(LOG_ERR, 0, "smb_init_addr_w is : %x, smb_init_addr_r is : %x cmdcode:%x value:%x\n",
  //smb_init_0.b.smb_init_addr_w,smb_init_0.b.smb_init_addr_r, cmd_code, value);                     //Shane Debug2
  smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
  writel(smb_init_0.all, &smb->smb_init_0);
  writel(0x05550562, &smb->smb_timer_0);
  writel(0x30551062, &smb->smb_timer_1);
  writel(0x4F112E2E, &smb->smb_timer_2);
  writel(0x112E2E05, &smb->smb_timer_3);
  writel(0x2E2E054F, &smb->smb_timer_4);
  writel(0x5B054F11, &smb->smb_timer_5);

  /* set the data*/
   //rtos_mmgr_trace(LOG_ERR, 0, "test value : %x \n", value);                                                                     //Shane Debug2
  smb_wr_data.b.smb_init_wr_data01 = value;
  //smb_wr_data.b.smb_init_wr_data02 = next_byte;   //make sure next byte is not poluted //put I2C_read *value into smb_init_wr_data02
  writel(smb_wr_data.all, &smb->smb_init_wr_1);

  //rtos_mmgr_trace(LOG_ERR, 0, "smb_wr_data.b.smb_init_wr_data01 / next_byte is: %x / %x \n", smb_wr_data.b.smb_init_wr_data01, next_byte);

  /* set smb enable and write word start */
  smb_control_register.all = 0;
  smb_control_register.b.smb_en = 0x01;
  //smb_control_register.b.smb_tran_i2c_en = 1;
  smb_control_register.b.smb_tran_tp_sel = 0x0A;//master mode, read, 0x6: master, write
  smb_control_register.b.smb_mas_tran_st = 0x01;
  smb_control_register.b.smb_slv_timer_opt = 0x01;
  writel(smb_control_register.all, &smb->smb_control_register);

  while (!(readl(&smb->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
      if (++cnt == 0x7FFFFFF) {
          cnt = 0;
          sts = readl(&smb->smb_intr_sts);
          rtos_smb_trace(LOG_ALW, 0xc044, "smb_intr_sts %x", sts);
      }
  }
  smb->smb_intr_sts.all = readl(&smb->smb_intr_sts);
  // when ACK_DONE_ERR, return hardcode value
  if (smb->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK)
      goto error;
  smb->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
  writel(smb->smb_intr_sts.all, &smb->smb_intr_sts);
  return 0x0;
error:
    //rtos_smb_trace(LOG_ERR, 0, "I2C_write error , register %xH data 0x%x", cmd_code, value);
  return 0xFFFFFFFF;
 #endif
}

slow_code void plp_iic_read_write(u8 slaveID, u8 cmd_code, u8 *value, u8 data, bool rw)
{
    u8 cnt = 0;
    u32 status = 0;

    do{
        status = rw ? I2C_read(slaveID, cmd_code, value) : I2C_write(slaveID, cmd_code, data);
        //rtos_smb_trace(LOG_DEBUG, 0, "status %x slaveID 0x%x cmd_code 0x%x",status, slaveID, cmd_code);
        if(!status) break;
        else if(status == 0xFFFFFFFE) continue;
        else if(rw == 0) break;
        else if(rw == 1) cnt++;
    }while(cnt <= 3);


    if(cnt == 4){
        esr_err_flags.b.iic_read_err = true;
        rtos_smb_trace(LOG_INFO, 0x80db, "I2C read retry err esr_err_flags %x",esr_err_flags.all);
        flush_to_nand(EVT_PLP_IC_ERR);
    }
    return;
}

#if ((Tencent_case) || (RD_VERIFY))
#if (FIX_I2C_SDA_ALWAYS_LOW_ISSUE == mENABLE)
#define PMIC_SLAVE 0x25
#define PMIC_ADD (PMIC_SLAVE << 1)
#define SDA_LOW_MASK (ARB_LOST_STS_MASK | MAS_TRAN_START_STS_MASK)

fast_code u32 hal_i2c_read_with_status(u8 slaveID, u8 cmd_code, u8 *value)
{
    if(I2C_read_lock == true)
        return 0xFFFFFFFE;
    I2C_read_lock = true;
    u32 data0;
    smb_init_0_t smb_init_0;
    smb_control_register_t smb_control_register;
    u32 cnt = 0x7FFFFF;
    u32 sts;
    u32 ret = SMB_OK;

    sts = readl(&smb->smb_intr_sts);

    lock_regulator();

    /* set init rd and wr address */
    smb_init_0.all = 0;
    smb_init_0.b.smb_init_addr_w = slaveID;
    smb_init_0.b.smb_init_addr_r = slaveID + 1;

    smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
    writel(smb_init_0.all, &smb->smb_init_0);

    /* set smb enable and read word start */
    smb_control_register.all = 0;
    smb_control_register.b.smb_en = 0x01;
    smb_control_register.b.smb_tran_tp_sel = 0x0B;//master mode, read, 0x6: master, write
    smb_control_register.b.smb_mas_tran_st = 0x01;
    smb_control_register.b.smb_slv_timer_opt = 0x01;
    writel(smb_control_register.all, &smb->smb_control_register);

    // temp printing before sensor issue clarified
    sts = readl(&smb->smb_intr_sts);
    while (!(sts & MAS_TRAN_DONE_NML_STS_MASK)) {
        sts = readl(&smb->smb_intr_sts);
        if ((++cnt == 0x7FFFFFF) || ((sts & SDA_LOW_MASK) == SDA_LOW_MASK)) {

            rtos_smb_trace(LOG_ALW, 0xcda2, " [I2C] intr_sts %x", sts);
            rtos_smb_trace(LOG_ALW, 0xdc5c, " [I2C] ToCnt %x", cnt);
            cnt = 0;

            if ((sts & SDA_LOW_MASK) == SDA_LOW_MASK)
            {
                ret = SMB_FAIL;
            }
            break;
        }
    }
    sts = readl(&smb->smb_intr_sts);
    writel(sts, &smb->smb_intr_sts);
    if (sts& WR_ACK_DONE_ERR_STS_MASK){
        rtos_smb_trace(LOG_ALW, 0xba50, " [I2C read error] intr_sts:%x slaveID 0x%x cmd_code 0x%x", sts, slaveID, cmd_code);
        writel(sts, &smb->smb_intr_sts);
        goto error;
    }
    data0 = readl(&smb->smb_pasv_0);
    *value = data0 >> 24;
    unlock_regulator();
    I2C_read_lock = false;
    Detect_FLAG = 0;
    return ret;
error:
    unlock_regulator();
    I2C_read_lock = false;
    Detect_FLAG =0;
    if (ret)
    {
        return SMB_FAIL;
    }
    else
    {
        return 0xFFFFFFFF;
    }
}

ddr_code void i2c_bus_validation_test(void)
{
	u8 data = 0;
	u32 status = SMB_OK;

	status = hal_i2c_read_with_status(PMIC_ADD, 0x34, &data);
	if (status == SMB_OK)
	{
		rtos_smb_trace(LOG_ALW, 0x983a, "I2C Bus available");
	}
	else
	{
		if (status == SMB_FAIL)
		{
			rtos_smb_trace(LOG_ALW, 0xca8d, "I2C Bus stuck");
			extern void gpio_set_gpio0(u32 value);
			gpio_set_gpio0(1);
			rtos_smb_trace(LOG_ALW, 0x4419, "Reset SoC");
			mdelay(1000);
			while(true);
		}
		else
		{
			//TODO
			rtos_smb_trace(LOG_ALW, 0xa23b, "ToDo");
		}
	}
}
#endif
#endif

//extern slow_code u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value);
//extern slow_code u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value);
ps_code int I2C_test(int argc, char *argv[])
{
    rtos_smb_trace(LOG_INFO, 0xf32f, "\n---\n");
    int WR = strtol(argv[1], (void *)0, 0);
    int slaveID = strtol(argv[2], (void *)0, 0); // Use 8-bit address !!, 2023.6.27
    int cmd_code = strtol(argv[3], (void *)0, 0);
    u8 value= strtol(argv[4], (void *)0, 0);

    writel(0x0000ffff, &smb->smb_intr_sts);
    u32 data = (u32)readl(&smb->smb_intr_sts);
    rtos_smb_trace(LOG_INFO, 0xba3c, "smb_intr_sts: %x", data);
    if (WR == 0) {
    plp_iic_read_write(slaveID, cmd_code, NULL, value, 0);
    rtos_smb_trace(LOG_INFO, 0xb712, "I2C_write(slaveID=%x, cmd_code=%x, value=%x, iic_read_err=%x)\n", slaveID, cmd_code, value, esr_err_flags.b.iic_read_err);
    }
    if (WR == 1){
    plp_iic_read_write(slaveID, cmd_code, &value, 0, 1);
    rtos_smb_trace(LOG_INFO, 0xa5c6, "I2C_read(slaveID=%x, cmd_code=%x, value=%x, iic_read_err=%x)\n", slaveID, cmd_code, value, esr_err_flags.b.iic_read_err);
    }

    return 0;

    }

static DEFINE_UART_CMD(I2C_test, "I2C_test",
        "I2C_test [WR] [slaveID] [cmd_code] [value]",
        "I2C_test(WR, slaveID, cmd_code, value)",
        0, 4, I2C_test)
;


#endif
