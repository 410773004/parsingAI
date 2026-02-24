#!/bin/bash

if [ $# -lt 1 ]; then
	echo "please enter your target: [ramdisk] [rawdisk]"
	exit
fi
cfg=""
echo "Configure Crypto engine?)"
echo "1. disable"
echo "2. use SM4 algorithm"
echo "3. use AES algorithm"
read crypto_cfg
if [ $crypto_cfg == "2" ]; then
	cfg="-DUSE_CRYPTO_HW=1 -DUSE_SM4_ALGO=1"
elif [ $crypto_cfg == "3" ]; then
	cfg="-DUSE_CRYPTO_HW=1"
fi

. board_config

. loader_ver
. tool_config

if [ $1 == "ramdisk" ]; then
	echo "prepare make env for ramdisk ... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDISABLE_HS_CRC_SUPPORT=0 -DUSE_512B_HOST_SECTOR_SIZE=1"
	fw=$1
elif [ $1 == "ramdisk_ddr" ]; then
	echo "prepare make env for ramdisk with ddr... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDDR_TEST=1 -DDDR=1 -DDISABLE_HS_CRC_SUPPORT=0 -DUSE_512B_HOST_SECTOR_SIZE=1"
	fw=$1
	. ddr_config
elif [ $1 == "ramdisk_full" ]; then
	echo "prepare make env for ramdisk with full available sram... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDISABLE_HS_CRC_SUPPORT=1 -DRAMDISK_FULL=1"
	fw=$1
elif [ $1 == "ramdisk_full_ddr" ]; then
	echo "prepare make env for ramdisk with full available ddr... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDDR_TEST=1 -DDDR=1 -DDISABLE_HS_CRC_SUPPORT=1 -DRAMDISK_FULL=1"
	fw=$1
	. ddr_config
elif [ $1 == "ramdisk_ddr_nopcie" ]; then
	echo "prepare make env for ramdisk with ddr without pcie... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDDR_TEST=1 -DDDR=1 -DDISABLE_HS_CRC_SUPPORT=1"
	fw=$1
	. ddr_config
elif [ $1 == "ramdisk_ns" ]; then
	echo "prepare make env for ramdisk 2ns ... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDDR_TEST=1 -DDDR=1 -DDISABLE_HS_CRC_SUPPORT=1 -DUSE_512B_HOST_SECTOR_SIZE=1 -DRAMDISK_2NS=1"
	fw=$1
	. ddr_config
elif [ $1 == "ramdisk_hmb" ]; then
	echo "prepare make env for ramdisk hmb dtag... "
	arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDISABLE_HS_CRC_SUPPORT=0 -DUSE_512B_HOST_SECTOR_SIZE=1 -DHMB_SUPPORT=1 -DHMB_DTAG=1"
	fw=$1
elif [ $1 == "rawdisk" ]; then
	echo "prepare G $1 ... "

	if [ $# -ne 2 ]; then
		echo "plz specific nand model mu/tsb!"
		exit
	else
		if [ $2 == "mu" ]; then
			arg_nand="-DUSE_MU_NAND=1"
		elif [ $2 == "tsb" ]; then
			arg_nand="-DUSE_TSB_NAND=1"
		elif [ $2 == "ymtc" ]; then
			arg_nand="-DUSE_YMTC_NAND=1"
		elif [ $2 == "hynx" ]; then
			arg_nand="-DUSE_HYNX_NAND=1"
			echo "1. 3DV4, 2. 3DV5"
			read hynx_type
			if [ $hynx_type == 1 ]; then
				arg_nand="$arg_nand -DUSE_8K_PAGE=1"
			fi

		elif [ $2 == "unic" ]; then
			arg_nand="-DUSE_UNIC_NAND=1"
		elif [ $2 == "sndk" ]; then
			arg_nand="-DUSE_SNDK_NAND=1"
		elif [ $2 == "ss" ]; then
			arg_nand="-DUSE_SS_NAND=1"
		elif [ $2 == "tsb-xl" ]; then
			arg_nand="-DUSE_TSB_NAND=1 -DTSB_XL_NAND=1"
		else
			echo "only support mu/tsb"
			exit 1
		fi
	fi

	arg="$cfg $arg_nand -DRAWDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1 -DDISABLE_HS_CRC_SUPPORT=0 -DUSE_512B_HOST_SECTOR_SIZE=1 -DSEMI_WRITE_ENABLE=0 -DUSE_8K_DU=0 -DPI_SUPPORT=0"

	echo "plz select rawdisk type :"
	echo "    1. rawdisk         (data use sram-dtag; l2p in sram)"
	echo "    2. rawdisk_ddr     (data use ddr-dtag;  l2p in sram)"
	echo "    3. rawdisk_l2p     (data use sram-dtag; l2p in ddr )"
	echo "    4. rawdisk_ddr_l2p (data use ddr-dtag;  l2p in ddr )"
	echo "    5. rawdisk_hmb     (data use hmb-dtag; l2p in sram)"

	read raw_type

	if [ $raw_type == "1" ]; then
		echo "prepare rawdisk"
		fw=$1_$2
	elif [ $raw_type == "2" ]; then
		echo "prepare rawdisk_ddr"
		arg="$arg -DDDR_TEST=1 -DDDR=1"
		fw=$1_ddr_$2
		. ddr_config
	elif [ $raw_type == "3" ]; then
		echo "prepare rawdisk_l2p"
		arg="$arg -DRAWDISK_L2P=1 -DDDR_TEST=1 -DDDR=1"
		fw=$1_l2p_$2
		. ddr_config
	elif [ $raw_type == "4" ]; then
		echo "prepare rawdisk_ddr_l2p"
		arg="$arg -DDDR_TEST=1 -DDDR=1 -DDISABLE_HS_CRC_SUPPORT=1 -DRAWDISK_L2P=1"
		fw=$1_ddr_l2p_$2
		. ddr_config
	elif [ $raw_type == "5" ]; then
		echo "prepare rawdisk_hmb_dtag"
		arg="$arg -DHMB_SUPPORT=1 -DHMB_DTAG=1"
		fw=$1_hmb_dtag_$2
	else
		echo "must select"
		exit 1
	fi
else
	echo "not support"
	exit 1
fi

# parse fw config in feature_config and output -DXXX in $arg
. feature_config

cwd=${PWD##*/}

dir=$cwd"-build"

cd ..

rm $dir -rf
mkdir $dir

cd $dir

echo $arg
cmake ../$cwd $arg

make -j16

$fw_creator -p -i main -d 0 -o $fw.fw
$fw_creator -p -b -l ../$cwd/scripts/$loader -f $fw.fw -o ${fw}_combo.fw
