#!/bin/bash
# Layout Regularization Scheme (LRS)

# use mpc-build.sh by default
progs=./mpc-build.sh

rm -rf .layout && mkdir -p .layout
cp build/gcc/ldscript/mem_* .layout
cp build/gcc/ldscript/sections_x.ld .layout

NR_CPUS=$1
let SRAM_BASE=0x20000A00
let SRAM_SH_SECTION=0x200B0000

SRAM_SIZE() {
	START=$(arm-none-eabi-readelf -S $1 | /bin/grep ps_code  | awk '{printf("0x%s",$5)}')
	END=$(arm-none-eabi-readelf -S $1 | /bin/grep sram_free  | awk '{printf("0x%s",$4)}')
	# alignment to 32 bytes
	SIZE=$((($END - $START + 31) / 32 * 32))
	echo $SIZE
}

if [ $# -ne 2 ]; then
	echo "USAGE>> ./mpc-build.sh 4 tsb"
	exit 0
fi

for i in `seq 1 $NR_CPUS`; do
	_SRAM_SIZE=$(SRAM_SIZE ../tacoma-bin/main$i)
	_SRAM_BASE=$((SRAM_BASE))
	SRAM_BASE=$(($_SRAM_BASE + $_SRAM_SIZE))

	SRAM_ATTR=$(printf "    SRAM    (rwx)  : ORIGIN = 0x%08X, LENGTH = 0x%08X" $_SRAM_BASE $_SRAM_SIZE)
	#echo $SRAM_ATTR

	sed -i -e "0,/.*SRAM    (rwx).*/ s/.*SRAM    (rwx).*/$SRAM_ATTR/" build/gcc/ldscript/mem_$i.ld
done

HEAP_BASE=$((($SRAM_BASE + 255) / 256 * 256))
HEAP_SIZE=$(($SRAM_SH_SECTION - $HEAP_BASE))
PER_HEAP_SIZE=$(($HEAP_SIZE / $NR_CPUS))
HEX_PER_HEAP_SIZE=$(printf "0x%08X" $PER_HEAP_SIZE)
for i in `seq 1 $NR_CPUS`; do
	_HEAP_BASE=$((HEAP_BASE))
	HEAP_BASE=$(($_HEAP_BASE + $PER_HEAP_SIZE))

	HEAP_ATTR=$(printf "HEAP    (rwx)  : ORIGIN = 0x%08X, LENGTH = 0x%08X" $_HEAP_BASE $PER_HEAP_SIZE)
	sed -i -e "/.*SRAM    (rwx).*/a\   \ ${HEAP_ATTR}" build/gcc/ldscript/mem_$i.ld
done

### if you want to uni-heap, use ${HEAP_SIZE} and then heap will be allocated with spin_lock
sed -i -e "s/\(.*__sram_free_size =\).*/\1 ${HEX_PER_HEAP_SIZE};/" build/gcc/ldscript/sections_x.ld
sed -i -e "/.*__sram_free_end.*=.*.;/{n;s/SRAM/HEAP/;}" build/gcc/ldscript/sections_x.ld

${progs} $1 $2
cp .layout/* build/gcc/ldscript/
rm -rf .layout
