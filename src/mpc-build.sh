#!/bin/bash

cnt=4
s=1
e=$cnt
opt="tsb"
configbin="./scripts/fwconfig.bin"
cust_fr=0
cust_fr_ver="000"
sec_revivion=0
sec_fr_ver="000"
loader_uart=0
model_func="P"
board_fea="000"

git_rev=$(git rev-parse --short HEAD)
echo "git_rev: $git_rev"
with_signature=false
key="./scripts/rsa_prif4.key"

while getopts "h?n:s:e:o:c:r:v:l:k" args; do
	case "$args" in
	h|\?)
		echo "-n [cpu] : CPU number"
		echo "-s [cpu_id] : build start CPU"
		echo "-e [cpu_id] : build end CPU"
		echo "-o ... : build option, tsb/mu for nand type, can use + to use more option, ex: tsb+pi8"
		echo "-k : use private key to sign image"
		exit 0
		;;
	n)  cnt=$OPTARG
		;;
	s)  s=$OPTARG
		;;
	e)  e=$OPTARG
		;;
	o)  opt=$OPTARG
		;;
	c)  configbin=$OPTARG
		;;
	v)  cust_fr_ver=$OPTARG
		cust_fr=1
		;;
	k)	with_signature=true
		;;
	l)	loader_uart=1
		;;
	r)  sec_fr_ver=$OPTARG
		sec_revivion=1
		;;
    esac
done

cwd=${PWD##*/}
echo "cwd: $cwd"

if [ $sec_revivion == 1 ]; then
echo "Security Rev: $sec_fr_ver"
sed -i "s/#define FW_SECURITY_VERSION_L .*/#define FW_SECURITY_VERSION_L 0x${sec_fr_ver}/g" ../src/inc/fw_download.h
sed -i "s/sv_padding_1 = .*/sv_padding_1 = 0x${sec_fr_ver}/g" ../src/scripts/shared_tool/img_builder/img_builder.py
fi

. board_config	# board_cfg, cfg, ddr_size_list, ddr_speed
if $with_signature; then
echo "It's security"
. Secure_loader_ver	# loader, Sloader
else
echo "No security"
. loader_ver	# loader, Sloader
fi
. tool_config	# fw_creator, fw_config, set_ddr_config

echo "build image ... from $s, MPC=$cnt, optional: $opt"
target="rdisk+hcrc+512b+semi+pxlc+secc+iecc"
declare -a imgs
imgs=($target"+nm+disable_wa6654" \
$target"+nm" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Add_smart" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+RD_VERIFY+Add_smart+_WUNC_" \
$target"+nm+disable_wa6654+mdot2+TCG+DST" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Synology_case+_WUNC_" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Tencent_case+Add_smart" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Xfusion_case+Add_smart" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Smart_Modular_case" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Lenovo_case" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+Baidu_case" \
$target"+nm+disable_wa6654+mdot2+TCG+DST+iEi_case" \
$target"+nm+pi8+disable_wa6654+mdot2" \
$target"+nm+pi8+disable_wa6654" \
$target"+l2c" \
$target"+aes" \
$target"+sm4" \
$target"+l2c+aes" \
$target"+l2c+sm4" \
$target"+l2c" \
$target"+l2c+aes" \
$target"+l2c+sm4" \
$target"+l2c+sm4+pi8" \
"rdisk+512b+semi+iecc"
$target"+nm+disable_wa6654+pi8+savecdump"\
)

if [[ $opt == *"rdisk"* ]]; then
	echo "target: $opt"
	fw=$opt
else
	i=0
	for img in ${imgs[*]}
	do
        	echo "$((i++)) : $img"
	        #fw=$img
	done
	imgs_cnt=${#imgs[@]}
	imgs_cnt=$((imgs_cnt - 1))
	echo "choose rdisk type: 0 ~ $imgs_cnt"
	read -p "Your input: " image_type
	if [[ $image_type -gt $imgs_cnt ]]; then
	        echo "no this images"
	        exit 1
	fi
	echo "image_type: $image_type"
	fw=${imgs[$image_type]}"+"$opt
	# . ddr_config
fi

fw=$fw"+"$board_fea

echo "fw: $fw"
arg="$cfg -DENABLE_SOUT=0 -DMPC=$cnt -DDDR=1 -DCUSTOMER=1 -DFREE_DTAG_PRELOAD=1 -DRDISK=1 -DUSE_CRYPTO_HW=1 -DNVME_TELEMETRY_LOG_PAGE_SUPPORT=1"
#TCG type: _TCG_ = TCG_SUPPORT
#               |- 0: None
#               |- 1: Opal
#               |- 2: EDrive

# parse fw config in feature_config
. feature_config	# 添加 arg

base=$arg
echo -e "\nbase: $base"
if [ $e == 4 ]; then
base="$base -DDUAL_BE=2"
echo "base: $base"
fi

if [ $cust_fr == 1 ]; then
. fr_config		# cust_fr_ver, cust_code
echo cust_fr_ver = $cust_fr_ver
cpu[0]=" -DBTN_MOD=1 -DFE_MOD=nvme -DDDR_TEST=1 -DCUST_FR=$cust_fr -DCUST_CODE=$cust_code -DSSSTC_FW_UPDATE=1"
cpu[1]=" -DBE_MOD=ncl_20 -DSCHEDULER=1 -DSSSTC_FW_UPDATE=1 -DCUST_FR=$cust_fr -DCUST_CODE=$cust_code"
cpu[2]=" -DFTL_MOD=ftl -DCUST_FR=$cust_fr -DCUST_CODE=$cust_code"
cpu[3]=" -DBE_MOD=ncl_20 -DSCHEDULER=2 -DBTN_MOD=1 -DSSSTC_FW_UPDATE=1 -DCUST_FR=$cust_fr -DCUST_CODE=$cust_code"
else
cpu[0]=" -DBTN_MOD=1 -DFE_MOD=nvme -DDDR_TEST=1 -DSSSTC_FW_UPDATE=1"
cpu[1]=" -DBE_MOD=ncl_20 -DSCHEDULER=1 -DSSSTC_FW_UPDATE=1"
cpu[2]=" -DFTL_MOD=ftl"
cpu[3]=" -DBE_MOD=ncl_20 -DSCHEDULER=2 -DBTN_MOD=1 -DSSSTC_FW_UPDATE=1"
fi

#cpu[1]=" -DBE_MOD=ncl_20 -DSCHEDULER=1 -DSSSTC_FW_UPDATE=1"
#cpu[2]=" -DFTL_MOD=ftl"
#cpu[3]=" -DBE_MOD=ncl_20 -DSCHEDULER=2 -DBTN_MOD=1 -DSSSTC_FW_UPDATE=1"

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

	if [ $i -gt 1 ]; then # copy trace file
		cp ../$dir1/trace-eventid.h .
		cp ../$dir1/trace-fmtstr.h .
	fi

	j=$((i-1))
	arg="$base -DCPU_ID=$i"

	arg="$arg ${cpu[$j]}"
	echo -e "\ni:$i, s:$s, e:$e, arg: $arg\n"
	cmake ../$cwd $arg
	cp ../$dir1/shared_mem.syms .

	make -j16 -f Makefile || exit 1
	cp ./trace-fmtstr.h ../$bin
	cp ./main$i ../$bin/main$i.elf
	cp ./output.map ../$bin/output$i.map
	cp *.rostr_data ../$bin
	cp *.elfinfo ../$bin
	cd ..
	cd $cwd

	img_cmd=$img_cmd"-i ../$bin/main$i.elf -d $((i-1)) "
done

#default setting,
#echo "FWCONFIG use default"
#select m2 or u2 loader
if [[ $fw == *"mdot2"* ]]; then
	echo "mdoat2 loader"
	rm ./scripts/LJ1_LDR.elf
	cp -r ./scripts/m2_LDR/* ./scripts/
else
	echo "udoat2 loader"
	rm ./scripts/LJ1_LDR.elf
	cp -r ./scripts/u2_LDR/* ./scripts/
fi
loader_cmd=$fw_creator" -p "
if $with_signature; then
	loader_img="loader_signed.fw"
	loader_cmd=$loader_cmd" -k $key -i ./scripts/$loader -d 0 -o ../$bin/$loader_img"
	fw=$fw"_signed"
	img_cmd=$img_cmd"-k $key "
else
	echo "There is no security"
fi
if $with_signature; then
	img_cmd=$img_cmd"-s -a -z -o ../$bin/$fw.fw"
else
	img_cmd=$img_cmd"-a -z -o ../$bin/$fw.fw"
#img_cmd=$img_cmd"-k ./scripts/rsa_pri-f4.key -o ../$bin/rdisk_mpc_${nand_type,,}.fw"
fi
echo "img_cmd: $img_cmd"
eval $img_cmd
if $with_signature; then
	echo "=============================="
	echo -e "\nloader_cmd: $loader_cmd"
	eval $loader_cmd
	echo "=============================="
else
	echo "There is no security"
fi

#Gen combo image
echo "CONFIG BIN use: $configbin"
if [[ $fw == *"lpddr4"* ]]; then
	dt="lpddr4"
else
	dt="ddr4"
fi

for ds in $ddr_size_list; do
	ds_mb=$ds"mb"
	ddr_speed_mt=${ddr_speed[$dt]}"mt"
	updated_fwcfg="../$bin/ddr-$ds_mb-$ddr_speed_mt-fwconfig.bin"
	cmd="$set_ddr_config -b $configbin -o $updated_fwcfg -s ${ddr_speed[$dt]} -z $ds -p $board_cfg"
	echo $cmd
	eval $cmd
	if $with_signature; then
		img_cmd=$fw_creator" -p -b -l ../$bin/$loader_img -f ../$bin/$fw.fw -c $updated_fwcfg -o ../$bin/"$fw"_$ds_mb-$ddr_speed_mt-combo.fw -r $cust_fr_ver"
	else
		img_cmd=$fw_creator" -p -b -l ./scripts/$loader -f ../$bin/$fw.fw -c $updated_fwcfg -o ../$bin/"$fw"_$ds_mb-$ddr_speed_mt-combo.fw -r $cust_fr_ver"
	fi
	#img_cmd=$fw_creator" -p -b -l ./scripts/$Sloader -f ../$bin/rdisk_mpc_${nand_type,,}.fw -c $configbin -o ../$bin/rdisk_mpc_${nand_type,,}_combo.fw"
	echo "img_cmd: $img_cmd"
	eval $img_cmd
done

cd ../$bin
if [ $cust_fr == 1 ]; then
	if $with_signature; then
   	   cp *4096*.fw "$cust_fr_ver"_signed.fw
	else
   	   cp *4096*.fw "$cust_fr_ver".fw
	fi
	sed -i "s/#define FR .*/#define FR \"EN2RD000\"/g" ../src/nvme/inc/nvme_cfg.h
fi

if [ $sec_revivion == 1 ]; then
echo "Security Rev: $sec_fr_ver"
sed -i "s/#define FW_SECURITY_VERSION_L .*/#define FW_SECURITY_VERSION_L 0x00000000/g" ../src/inc/fw_download.h
sed -i "s/sv_padding_1 = .*/sv_padding_1 = 0x00000000/g" ../src/scripts/shared_tool/img_builder/img_builder.py
fi

if [ $cust_fr == 1 ]; then
	if $with_signature; then
	tar cvf sout_"$cust_fr_ver"_signed.tar *.elfinfo *.rostr_data trace-fmtstr.h
	tar cvf jlink_"$cust_fr_ver"_signed.tar *.elf *.map
	else
	tar cvf sout_"$cust_fr_ver".tar *.elfinfo *.rostr_data trace-fmtstr.h
	tar cvf jlink_"$cust_fr_ver".tar *.elf *.map
	fi
else
	tar cvf sout_"$git_rev".tar *.elfinfo *.rostr_data trace-fmtstr.h
	tar cvf jlink_"$git_rev".tar *.elf *.map
fi

rm *.elfinfo *.rostr_data

echo $fw >> ../$bin/config
exit 0
