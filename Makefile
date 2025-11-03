all:
	STAGING_DIR=$(pwd)/staging_dir/ staging_dir/toolchain-arm_arm1176jzf-s+vfp_gcc-14.3.0_musl_eabi/bin/arm-openwrt-linux-gcc -Istaging_dir/target-arm_arm1176jzf-s+vfp_musl_eabi/usr/include -Lstaging_dir/target-arm_arm1176jzf-s+vfp_musl_eabi/usr/lib -lgpiod -luci -lubox -lubus  ../blink.c -o ../blin
