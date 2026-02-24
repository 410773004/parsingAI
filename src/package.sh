#!/bin/bash

if [[ $# != 2 ]]; then
	echo "ex: ./package.sh tsb rdisk+semi+hcrc+512b+ddr4"
	exit 0
fi

cwd=${PWD##*/}
echo "now building programmer ... "
./programmer-build.sh -o $1

echo "now building firmware ... "
if [[ $2 == *"rdisk"* ]]; then
	./mpc-build.sh -o $2+$1
	bin="bin"
elif [[ $2 == *"ramdisk"* ]]; then
	./ramdisk-build.sh -o $2
	bin="build"
elif [[ $2 == *"rawdisk"* ]]; then
	./rawdisk-build.sh -o $2+$1
	bin="build"
fi

rm ../package -rf
mkdir ../package
cp ./scripts/shared_tool/uart/dist/muxterm.zip ../package
cp ./scripts/release/utils/* ../package -r
cp ./scripts/down.sh ../package
cp ./scripts/down.bat ../package
cp ../$cwd-programmer/*.fw ../package
cp ../$cwd-programmer/*.elf ../package
cp ../$cwd-$bin/*.fw ../package
cp ../$cwd-$bin/*.elf ../package
cp ../$cwd-$bin/*.tar ../package
cp ../$cwd-$bin/*.map ../package



