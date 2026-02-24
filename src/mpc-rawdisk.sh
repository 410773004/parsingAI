#!/bin/bash

cnt=2
s=1
e=$cnt
opt="tsb"

. spc

cwd=${PWD##*/}

. board_config
. loader_ver
. tool_config

#Force to set to uppercase(tsb→TSB, mu→MU)
echo "build image ... from $s, MPC=$cnt, optional: $opt"
target="rawdisk+mpc+512b"

nosups=(full 8kdu aes sm4 ddr_half \
hcrc semi pi8 hmeta8 pi16 pecc iecc \
merge_wa l2c atomic nm qlc lpddr \
512m 1g 2g 4g 8g raid pxlc)

for nosup in ${nosups[@]}
do
	if [[ $opt == *"$nosup"* ]]; then
		echo "mpc-rawdisk does not support."
		exit 1
	fi
done

if [[ $opt == *"rawdisk+mpc"* ]]; then
	echo "target: $opt"
	fw=$opt
else
	fw=$target"+"$opt
fi

if [[ $fw == *"ddr"* ]]; then
	. ddr_config
fi

echo $fw
arg="$cfg -DENABLE_SOUT=0 -DUSE_TSB_NAND=1 -DMPC=$cnt -DCUSTOMER=1 -DRAWDISK=1 -DBTN_STREAM_BUF_ONLY=1 "
#base="$base -DDDR=1"
#base="$base -DTSB_XL_NAND=1"
#base="$base -DQLC_SUPPORT=1"
#base="$base -DFORCE_IO_SRAM=1"

# parse fw config in feature_config
. feature_config

base=$arg

cpu[0]=" -DBTN_MOD=1 -DFE_MOD=nvme"
cpu[1]=" -DBTN_MOD=1 -DBE_MOD=ncl_20 -DNCB=1"
cpu[2]=" -DBE_MOD=ncl_20 -DNCB=2"
cpu[3]=""

bin=$cwd"-bin"
rm ../$bin -rf
mkdir ../$bin
dir1=$cwd"-build1"

img_cmd=$fw_creator" -p "
for (( i=$s; i<=$e; i++ ))
do
	dir=$cwd"-build"$i
	cd ..
	rm $dir -rf
	mkdir $dir
	cd $dir
	j=$((i-1))
	arg="$base -DCPU_ID=$i"

	arg="$arg ${cpu[$j]}"
	echo "$arg"
	cmake ../$cwd $arg
	cp ../$dir1/shared_mem.syms .

	make -j16 || exit 1
	cp ./trace-fmtstr.h ./main* ../$bin
	cp ./output.map ../$bin/output$i.map
	cd ..
	cd $cwd
	img_cmd=$img_cmd"-i ../$bin/main$i -d $((i-1)) "
done
img_cmd=$img_cmd"-o ../$bin/$fw.fw"
#img_cmd=$img_cmd"-k ./scripts/rsa_pri-f4.key -o ../$bin/rawdisk_${nand_type,,}.fw"
echo $img_cmd
eval $img_cmd

# Gen combo image
img_cmd=$fw_creator" -p -b -l ./scripts/$loader -f ../$bin/$fw.fw -o ../$bin/$fw-combo.fw"
#img_cmd=$fw_creator" -p -b -l ./scripts/$Sloader -f ../$bin/rawdisk_${nand_type,,}.fw -o ../$bin/rawdisk_mpc_${nand_type,,}_combo.fw"
echo $img_cmd
eval $img_cmd

cd ../$bin
tar cvf sout.tar *.elfinfo *.rostr_data trace-fmtstr.h
rm *.elfinfo *.rostr_data

echo $fw >> ../$bin/config

