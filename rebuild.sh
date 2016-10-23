# ! /bin/bash
# run this script to rebuild RefOS code.

make clean
make kzm_debug_defconfig
make silentoldconfig
make
