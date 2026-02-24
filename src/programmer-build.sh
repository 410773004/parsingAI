#!/bin/bash

. spc
. board_config
. tool_config

nands=(tsb mu b27b 3dv4 3dv5)
for nand in ${nands[@]}
do
	if [[ $opt == *"$nand"* ]]; then
		nand_type=$nand;
		break;
	fi
done
if [ -z $nand_type ]; then
	echo "Please enter NAND type: ${nands[@]}"
	exit
fi

# parse fw config in feature_config and output -DXXX in $arg
if [[ $opt == *"programmer"* ]]; then
	fw=$opt
else
	fw="programmer+ddr+$opt"
	if [[ $opt != *"ddr4"* ]]; then
		. ddr_config
	fi
fi

. feature_config

arg="$arg $cfg -DENABLE_SOUT=1 -DPROGRAMMER=1 -DCUSTOMER=1"
echo $arg
cwd=${PWD##*/}

dir=$cwd"-programmer"
cd ..
rm $dir -rf
mkdir $dir
cd $dir

echo $fw
cmake ../$cwd/ $arg
make -j 16 || exit 1
cp main $fw.elf
$fw_creator -p -i main -d 0 -o $fw.fw
echo "rainier $fw built successfully"
exit 0
