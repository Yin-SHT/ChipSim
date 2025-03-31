# 设置当前目录路径变量
CURRENT_DIR := $(shell pwd)

TOOLCHAIN_PREFIX=/home/yin/riscv/bin
VP_PATH=/home/yin/code/riscv-vp/vp/build/bin
CONFIG_PATH=/home/yin/code/riscv-vp/vp/src/noxim/config_examples

all : main.c sum.c bootstrap.S
	$(TOOLCHAIN_PREFIX)/riscv64-unknown-elf-gcc main.c sum.c bootstrap.S -o main -march=rv64g -mabi=lp64d -nostartfiles -Wl,--no-relax
	
sim: all
	$(VP_PATH)/tiny64-vp --intercept-syscalls main
	
dump-elf: all
	$(TOOLCHAIN_PREFIX)/riscv64-unknown-elf-readelf -a main
	
dump-code: all
	$(TOOLCHAIN_PREFIX)/riscv64-unknown-elf-objdump -D main
	
dump-comment: all
	$(TOOLCHAIN_PREFIX)/riscv64-unknown-elf-objdump -s --section .comment main
	
noc:
	$(VP_PATH)/tiny64-vp-noc -config $(CONFIG_PATH)/default_configMeshNoHUB.yaml -power $(CONFIG_PATH)/power.yaml -pe $(CONFIG_PATH)/pe.yaml -elf $(CURRENT_DIR)/main

clean:
	rm -f main
