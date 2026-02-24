#!/bin/bash

. spc

cfg=""

. tool_config
. board_config
. loader_ver

declare -a imgs
imgs=(ramdisk+sram+4k \
ramdisk+sram+4k+full \
ramdisk+sram+4k+hcrc \
ramdisk+sram+4k+aes \
ramdisk+sram+4k+hcrc+aes \
ramdisk+sram+4k+pi8 \
ramdisk+sram+4k+hmeta8 \
ramdisk+sram+4k+pi16 \
ramdisk+sram+4k+hmeta16 \
ramdisk+sram+512b \
ramdisk+sram+512b+pi8 \
ramdisk+sram+512b+pi16 \
ramdisk+sram+512b+hcrc \
ramdisk+sram+512b+aes \
ramdisk+sram+512b+hcrc+aes \
ramdisk+sram+8kdu \
ramdisk+sram+hcrc+8kdu \
ramdisk+sram+512b+8kdu \
ramdisk+sram+512b+hcrc+8kdu \
ramdisk+ddr+4k \
ramdisk+ddr+4k+full \
ramdisk+ddr+4k+hcrc \
ramdisk+ddr+4k+aes \
ramdisk+ddr+4k+hcrc+aes \
ramdisk+ddr+512b \
ramdisk+ddr+512b+hcrc \
ramdisk+ddr+512b+aes \
ramdisk+ddr+8kdu \
ramdisk+ddr+512b+8kdu \
ramdisk+ddr+512b+hcrc+aes \
ramdisk+ddr+iecc+4k \
ramdisk+ddr+iecc+4k+hcrc \
ramdisk+ddr+iecc+4k+aes \
ramdisk+ddr+iecc+4k+full \
ramdisk+ddr+iecc+4k+hcrc+aes \
ramdisk+ddr+iecc+512b \
ramdisk+ddr+iecc+512b+hcrc \
ramdisk+ddr+iecc+512b+aes \
ramdisk+ddr+iecc+8kdu \
ramdisk+ddr+iecc+512b+8kdu \
ramdisk+ddr+iecc+512b+hcrc+aes \
ramdisk+ddr+pecc+4k \
ramdisk+ddr+pecc+4k+full \
ramdisk+ddr+pecc+4k+hcrc \
ramdisk+ddr+pecc+4k+aes \
ramdisk+ddr+pecc+4k+hcrc+aes \
ramdisk+ddr+pecc+512b \
ramdisk+ddr+pecc+512b+hcrc \
ramdisk+ddr+pecc+512b+aes \
ramdisk+ddr+pecc+8kdu \
ramdisk+ddr+pecc+512b+8kdu \
ramdisk+ddr+pecc+512b+hcrc+aes \
)

if [[ $opt != *"ramdisk"* ]]; then
	i=0
	for img in ${imgs[*]}
	do
		echo "$((i++)) : $img"
		#fw=$img
	done
	imgs_cnt=${#imgs[@]}
	imgs_cnt=$((imgs_cnt - 1))
	echo "choose ramdisk type: 0 ~ $imgs_cnt"
	if [ $# -ge 1 ]; then
 		echo "additional option: $opt"
	fi
	read image_type

	if [[ $image_type -gt $imgs_cnt ]]; then
		echo "no this images"
		exit 1
	fi

	echo $image_type
	fw=${imgs[$image_type]}
	if [ $# -ge 1 ]; then
		fw="$fw+$opt"
	fi

	if [[ $fw == *"ddr"* ]]; then
		. ddr_config
	fi
else
	fw=$opt
fi
echo $fw

arg="$cfg -DRAMDISK=1 -DENABLE_SOUT=1 -DCPU_ID=1 -DCUSTOMER=1"

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
cp main $fw.elf
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

cp ../$cwd/scripts/gdb . -r
exit 0
