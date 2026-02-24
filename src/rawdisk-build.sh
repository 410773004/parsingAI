#!/bin/bash

. spc

. tool_config
. board_config
. loader_ver

declare -a imgs

imgs=(rawdisk+sram+4k \
rawdisk+sram+4k+hcrc \
rawdisk+sram+4k+aes \
rawdisk+sram+4k+hcrc+aes \
rawdisk+sram+4k+semi+hcrc+interbuf+aes \
rawdisk+sram+4k+8kdu \
rawdisk+sram+4k+hcrc+8kdu \
rawdisk+sram+4k+aes+8kdu \
rawdisk+sram+4k+hcrc+aes+8kdu \
rawdisk+sram+512b \
rawdisk+sram+512b+pi8 \
rawdisk+sram+512b+hcrc \
rawdisk+sram+512b+aes \
rawdisk+sram+512b+hcrc+aes \
rawdisk+sram+512b+semi \
rawdisk+sram+512b+interbuf \
rawdisk+sram+512b+hcrc+interbuf \
rawdisk+sram+512b+semi+interbuf \
rawdisk+sram+512b+semi+hcrc \
rawdisk+sram+512b+semi+hcrc+interbuf \
rawdisk+sram+512b+semi+aes \
rawdisk+sram+512b+semi+hcrc+aes \
rawdisk+sram+512b+semi+hcrc+interbuf+aes \
rawdisk+sram+512b+semi+hcrc+interbuf+aes+pi8 \
rawdisk+sram+512b+interbuf \
rawdisk+sram+512b+8kdu \
rawdisk+sram+512b+hcrc+8kdu \
rawdisk+sram+512b+aes+8kdu \
rawdisk+sram+512b+hcrc+aes+8kdu \
rawdisk+sram+512b+semi+hcrc+interbuf+aes+8kdu \
rawdisk+ddr+4k \
rawdisk+ddr+4k+hcrc \
rawdisk+ddr+4k+aes \
rawdisk+ddr+4k+semi+aes \
rawdisk+ddr+4k+semi+sm4 \
rawdisk+ddr+4k+hcrc+aes \
rawdisk+ddr+4k+8kdu \
rawdisk+ddr+4k+hcrc+8kdu \
rawdisk+ddr+4k+semi \
rawdisk+ddr+4k+semi+hcrc \
rawdisk+ddr+4k+semi+hcrc+aes \
rawdisk+ddr+4k+semi+interbuf \
rawdisk+ddr+4k+semi+interbuf+aes+hcrc \
rawdisk+ddr+4k+aes+8kdu \
rawdisk+ddr+4k+hcrc+aes+8kdu \
rawdisk+ddr+512b \
rawdisk+ddr+512b+hcrc \
rawdisk+ddr+512b+aes \
rawdisk+ddr+512b+interbuf \
rawdisk+ddr+512b+hcrc+aes \
rawdisk+ddr+512b+hcrc+pecc \
rawdisk+ddr+512b+hcrc+interbuf \
rawdisk+ddr+512b+semi \
rawdisk+ddr+512b+semi+interbuf \
rawdisk+ddr+512b+semi+interbuf+hcrc \
rawdisk+ddr+512b+semi+interbuf+aes \
rawdisk+ddr+512b+semi+interbuf+hcrc+aes \
rawdisk+ddr+512b+semi+interbuf+hcrc+aes+pi8 \
rawdisk+ddr+512b+semi+hcrc \
rawdisk+ddr+512b+semi+aes \
rawdisk+ddr+512b+semi+hcrc+aes \
rawdisk+ddr+512b+8kdu \
rawdisk+ddr+512b+hcrc+8kdu \
rawdisk+ddr+512b+aes+8kdu \
rawdisk+ddr+512b+semi+8kdu \
rawdisk+ddr+512b+interbuf+8kdu \
rawdisk+ddr+512b+hcrc+aes+8kdu \
rawdisk+ddr+512b+semi+interbuf+8kdu \
rawdisk+ddr+512b+semi+interbuf+aes+hcrc+8kdu \
rawdisk+ddr+iecc+4k \
rawdisk+ddr+iecc+4k+hcrc \
rawdisk+ddr+iecc+4k+aes \
rawdisk+ddr+iecc+4k+semi+aes \
rawdisk+ddr+iecc+4k+semi+sm4 \
rawdisk+ddr+iecc+4k+hcrc+aes \
rawdisk+ddr+iecc+4k+8kdu \
rawdisk+ddr+iecc+4k+hcrc+8kdu \
rawdisk+ddr+iecc+4k+semi \
rawdisk+ddr+iecc+4k+semi+hcrc \
rawdisk+ddr+iecc+4k+semi+hcrc+aes \
rawdisk+ddr+iecc+4k+semi+interbuf+hcrc \
rawdisk+ddr+iecc+4k+semi+interbuf+hcrc+aes \
rawdisk+ddr+iecc+4k+semi+interbuf \
rawdisk+ddr+iecc+4k+aes+8kdu \
rawdisk+ddr+iecc+4k+hcrc+aes+8kdu \
rawdisk+ddr+iecc+4k+interbuf+hcrc+aes \
rawdisk+ddr+iecc+512b \
rawdisk+ddr+iecc+512b+hcrc \
rawdisk+ddr+iecc+512b+aes \
rawdisk+ddr+iecc+512b+hcrc+aes \
rawdisk+ddr+iecc+512b+hcrc+aes+interbuf \
rawdisk+ddr+iecc+512b+hcrc+pecc \
rawdisk+ddr+iecc+512b+semi \
rawdisk+ddr+iecc+512b+semi+interbuf \
rawdisk+ddr+iecc+512b+semi+interbuf+hcrc \
rawdisk+ddr+iecc+512b+semi+interbuf+aes \
rawdisk+ddr+iecc+512b+semi+interbuf+hcrc+aes \
rawdisk+ddr+iecc+512b+semi+interbuf+hcrc+aes+pi8 \
rawdisk+ddr+iecc+512b+semi+interbuf+hcrc+aes+pi16 \
rawdisk+ddr+iecc+512b+semi+hcrc \
rawdisk+ddr+iecc+512b+semi+aes \
rawdisk+ddr+iecc+512b+semi+hcrc+aes \
rawdisk+ddr+iecc+512b+8kdu \
rawdisk+ddr+iecc+512b+hcrc+8kdu \
rawdisk+ddr+iecc+512b+aes+8kdu \
rawdisk+ddr+iecc+512b+semi+8kdu \
rawdisk+ddr+iecc+512b+interbuf+8kdu \
rawdisk+ddr+iecc+512b+hcrc+aes+8kdu \
rawdisk+ddr+iecc+512b+semi+interbuf+8kdu \
rawdisk+ddr+iecc+512b+semi+interbuf+aes+hcrc+8kdu \
rawdisk+ddr+pecc+4k \
rawdisk+ddr+pecc+4k+hcrc \
rawdisk+ddr+pecc+4k+aes \
rawdisk+ddr+pecc+4k+semi+aes \
rawdisk+ddr+pecc+4k+semi+sm4 \
rawdisk+ddr+pecc+4k+hcrc+aes \
rawdisk+ddr+pecc+4k+8kdu \
rawdisk+ddr+pecc+4k+hcrc+8kdu \
rawdisk+ddr+pecc+4k+semi \
rawdisk+ddr+pecc+4k+semi+hcrc \
rawdisk+ddr+pecc+4k+semi+hcrc+aes \
rawdisk+ddr+pecc+4k+semi+interbuf \
rawdisk+ddr+pecc+4k+aes+8kdu \
rawdisk+ddr+pecc+4k+hcrc+aes+8kdu \
rawdisk+ddr+pecc+512b \
rawdisk+ddr+pecc+512b+hcrc \
rawdisk+ddr+pecc+512b+aes \
rawdisk+ddr+pecc+512b+hcrc+aes \
rawdisk+ddr+pecc+512b+hcrc+pecc \
rawdisk+ddr+pecc+512b+semi \
rawdisk+ddr+pecc+512b+semi+interbuf \
rawdisk+ddr+pecc+512b+semi+interbuf+hcrc \
rawdisk+ddr+pecc+512b+semi+interbuf+aes \
rawdisk+ddr+pecc+512b+semi+interbuf+hcrc+aes \
rawdisk+ddr+pecc+512b+semi+interbuf+hcrc+aes+pi8 \
rawdisk+ddr+pecc+512b+semi+hcrc \
rawdisk+ddr+pecc+512b+semi+aes \
rawdisk+ddr+pecc+512b+semi+hcrc+aes \
rawdisk+ddr+pecc+512b+8kdu \
rawdisk+ddr+pecc+512b+hcrc+8kdu \
rawdisk+ddr+pecc+512b+aes+8kdu \
rawdisk+ddr+pecc+512b+hcrc+aes+8kdu \
)

echo $#
if [ $# -lt 1 ]; then
	echo "plz specific nand model mu/tsb/b27b at least!"
	exit 1
fi

if [[ $opt != *"rawdisk"* ]]; then
	i=0
	for img in ${imgs[*]}
	do
		echo "$((i++)) : $img"
	done
	imgs_cnt=${#imgs[@]}
	imgs_cnt=$((imgs_cnt - 1))
	echo "choose rawdisk type: 0 ~ $imgs_cnt"
	echo "additional option: $opt"
	read image_type
	if [[ $image_type -gt $imgs_cnt ]]; then
		echo " no this image"
		exit 1
	fi
	echo $image_type
	fw=${imgs[$image_type]}
	fw="$fw+$opt"

	if [[ $fw == *"ddr"* ]]; then
		. ddr_config
	fi
else
	fw=$opt
fi
echo $fw

# parse fw config in feature_config and output -DXXX in $arg
. feature_config

if [[ $fw == *"interbuf"* ]]; then
	echo "INTBUF"
	arg="$arg -DBTN_STREAM_BUF_ONLY=1"
fi

arg="$arg $arg_nand $cfg -DRAWDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1"

cwd=${PWD##*/}

dir=$cwd"-build"

cd ..
#fw="$fw_$arg_nand"
rm $dir -rf
mkdir $dir

cd $dir

echo $arg
cmake ../$cwd $arg

make -j16
$fw_creator -i main -d 0 -o $fw.fw

configbin="../$cwd/scripts/spc_fwconfig.bin"
echo "CONFIG BIN use: $configbin"
if [[ $fw == *"ddr"* ]]; then
	if [[ $fw == *"lpddr4"* ]]; then
		dt="lpddr4"
	else
		dt="ddr4"
	fi
	for ds in $ddr_size_list; do
		ds_mb=$ds"mb"
		ddr_speed_mt=${ddr_speed[$dt]}"mt"
		updated_fwcfg="ddr-$ds_mb-$ddr_speed_mt-fwconfig.bin"
		cmd="$set_ddr_config -b $configbin -o $updated_fwcfg -s ${ddr_speed[$dt]} -z $ds -p $board_cfg"
		echo $cmd
		eval $cmd

		img_cmd=$fw_creator" -p -b -l ../$cwd/scripts/$loader -f $fw.fw -c $updated_fwcfg -o "$fw"_$ds_mb-$ddr_speed_mt-combo.fw"
		#img_cmd=$fw_creator" -p -b -l ./scripts/$Sloader -f ../$bin/rdisk_mpc_${nand_type,,}.fw -c $configbin -o ../$bin/rdisk_mpc_${nand_type,,}_combo.fw"
		echo $img_cmd
		eval $img_cmd
	done
else
	img_cmd="$fw_creator -p -b -l ../$cwd/scripts/$loader -f $fw.fw -c $configbin -o $fw-combo.fw"
	echo $img_cmd
	eval $img_cmd
fi

cp main $fw.elf
cp ../$cwd/scripts/gdb . -r
echo $arg
exit 0
