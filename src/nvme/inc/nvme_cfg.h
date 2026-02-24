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

#pragma once

/* Constants */
#define NVME_VS_MAJOR                  1
#define NVME_VS_MINOR                  4

#define NVMET_NR_IO_QUEUE              8

#define NVMET_NR_INT_VECTORS           9
#define NVMET_NR_INT_VECTORS_NORMAL_MODE           66

#define IDTFY_RAB                      2
#define IDTFY_ELPE                     63
#define IDTFY_NPSS                     1///4
#define IDFTY_ACL                      4
#define IDFTY_AERL                     3
#define MAX_NPSS_NUM                   32
#define NVME_MTFA_LIMIT                0x64 //FWUP-3 The Maximum Time for Firmware Activation (MTFA) field shall not exceed 64h.
#if (Smart_Modular_case)
#define VID                            0x1235
#define DID                            0x4100
#else
#define VID                            0x1E95
#define DID                            0x100D
#endif
#if (CUST_FR == 1)
#if (Synology_case)
#define SSVID                          0x7053
#define SSDID                          0x4003
#elif (Smart_Modular_case)
#define SSVID                          0x1235
#define SSDID                          0x4100
#else
#define SSVID                          0x1E95
#define SSDID                          0x1101
#endif
#endif


#if (Smart_Modular_case)
#define IeeeU                          0x00
#define IeeeM                          0x05
#define IeeeL                          0x16
#else
#define IeeeU                          0x38
#define IeeeM                          0xF6
#define IeeeL                          0x01
#endif
#define DISABLE_SECURITY               0

#define MAKE_STR(x) _MAKE_STR(x)
#define _MAKE_STR(x) #x

//#define MAX_TEMP_SENSOR 3

#if defined(RAWDISK)
#define SN                          "SSD-DRAM-120216-R"
#elif defined(RDISK)
#if defined(HMB_SUPPORT)
#define SN                          "SSD-DRAM-120216-H"  // FTK with HMB
#else
#define SN                          "SSD-DRAM-120216-F"  // FTK without HMB
#endif
#elif defined(PROGRAMMER)
#define SN                          "SSD-DRAM-120216-V"
#else
#define SN                          "SSD-DRAM-120216-N"
#endif


#define SUBNQN                      "nqn.2016-11.com.innogrit:nvme.rainier"

#define FR "EN2RD000"

//add by suda
#define BV "ver 0.1 "
#define MV1 "E4M3412 " 
#define MV2 "E4M3413 " 
//
#define CNTID                          0

#define MAX_FWSLOT                     1

#if defined(PROGRAMMER)
#define NVME_SHASTA_MODE_ENABLE
#endif

/*Firmware Update Granularity (FWUG)*/
#define FWUG		1	///< The value is reported in 4 KiB units (e.g., 1h corresponds to 4 KiB, 2h corresponds to 8 KiB).
#define FWUG_SZ		(FWUG * 4096)	///< byte size of FWUG

//#define MQES		4095		/* ASIC I/O support up to 4096 */


#if NVME_TELEMETRY_LOG_PAGE_SUPPORT /*telemetry log page support attribute*/
#define TELEMETRY_SECTOR_SIZE (512)
#endif
