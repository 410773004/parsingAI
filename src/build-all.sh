#!/bin/bash

zipname=$(git rev-parse HEAD)

cwd=${PWD##*/}
build_dir=$cwd"-build"
dir=$cwd"-all"
rm ../$dir -rf
mkdir ../$dir

for i in {0..100}
do
	{ echo $i; } | ./ramdisk-build.sh $1
	if [[ $? == 1 ]]; then
		break
	fi

	cp ../$build_dir/*.elf ../$dir
done

mkdir ../$dir/tsb
for i in {0..200}
do
	{ echo $i; } | ./rawdisk-build.sh tsb $1
	if [[ $? == 1 ]]; then
		break
	fi

	cp ../$build_dir/*.elf ../$dir/tsb
done

mkdir ../$dir/mu
for i in {0..200}
do
	{ echo $i; } | ./rawdisk-build.sh mu $1
	if [[ $? == 1 ]]; then
		break
	fi

	cp ../$build_dir/*.elf ../$dir/mu
done
zipname="$zipname-$1"
zip ../$zipname ../$dir/* -r
echo "$zipname.zip"


