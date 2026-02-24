#!/bin/bash

zipname=$(git rev-parse HEAD)

cwd=${PWD##*/}
bin_dir=$cwd"-bin"	# rdisk binary folder
dir=$cwd"-all"		# top folder for all config
rm ../$dir -rf
mkdir ../$dir/
mkdir ../$dir/mu
mkdir ../$dir/mu_b27b
mkdir ../$dir/tsb

for i in {0..20}
do
	{ echo 3; echo $i; } | ./mpc-build.sh 4 tsb
	if [[ $? == 1 ]]; then
		break
	fi
	cfgfile="../"$bin_dir"/config"
	cfg=$(<$cfgfile)
	cp ../$bin_dir ../$dir/tsb/$cfg -r
done

for i in {0..20}
do
	{ echo 3; echo $i; } | ./mpc-build.sh 4 mu
	if [[ $? == 1 ]]; then
		break
	fi
	cfgfile="../"$bin_dir"/config"
	cfg=$(<$cfgfile)
	cp ../$bin_dir ../$dir/mu/$cfg -r
done

for i in {0..20}
do
	{ echo 3; echo $i; echo "y"; } | ./mpc-build.sh 4 mu
	if [[ $? == 1 ]]; then
		break
	fi
	cfgfile="../"$bin_dir"/config"
	cfg=$(<$cfgfile)
	cp ../$bin_dir ../$dir/mu_b27b/$cfg -r
done

zipname="$zipname-rdisk"
zip ../$zipname ../$dir/* -r
echo "$zipname.zip"


