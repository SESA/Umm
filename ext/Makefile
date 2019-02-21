EXT_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# solo5
SOLO5_DIR ?= $(EXT_PATH)/solo5
SOLO5_OBJ ?= $(SOLO5_DIR)/kernel/ebbrt/solo5.o

# rumprun
RUMP_DIR ?= $(EXT_PATH)/rumprun
RUMP_BAKE = $(RUMP_DIR)/rumprun-solo5/bin/rumprun-bake
RUMP_COOKFS = $(RUMP_DIR)/rumprun-solo5/bin/x86_64-rumprun-netbsd-cookfs
RUMP_SOLO5_X86_64=$(RUMP_DIR)/rumprun-solo5/rumprun-x86_64/lib
RUMP_SOLO5_UKVM=$(RUMP_SOLO5_X86_64)/rumprun-solo5/libsolo5_ukvm.a

RUMPRUN_CC= $(RUMP_DIR)/rumprun-solo5/bin/x86_64-rumprun-netbsd-gcc
RUMPRUN_CXX= $(RUMP_DIR)/rumprun-solo5/bin/x86_64-rumprun-netbsd-g++
RUMPRUN_CFLAGS =-Wall -Werror -Wmissing-prototypes -Wstrict-prototypes -g
RUMPRUN_CXXFLAGS =-Wall -Werror -g
RUMPRUN_LDLIBS= -lrumprun_tester

# rumprun-packages
RUMPPACKDIR ?= $(EXT_PATH)/rumprun-packages
RUMPCONFIG = $(RUMPPACKDIR)/config.mk

#	rumprun-nodejs
NODEJS_PATH ?= $(RUMPPACKDIR)/nodejs/
NODEJS_BIN=$(NODEJS_PATH)/build-4.3.0/out/Release/node

all: solo5 nodejs

solo5: $(SOLO5_OBJ)

rumprun: $(RUMP_SOLO5_X86_64) $(RUMP_SOLO5_UKVM)

nodejs: $(NODEJS_BIN)

$(SOLO5_OBJ):
	$(MAKE) -C $(SOLO5_DIR) ebbrt

$(SOLO5_HW): $(SOLO5_OBJ)

$(RUMP_SOLO5_X86_64):
	cd $(RUMP_DIR) && git submodule update --init
	$(MAKE) -C $(RUMP_DIR) build 

$(RUMP_SOLO5_UKVM): $(SOLO5_OBJ) | $(RUMP_SOLO5_X86_64)
	$(INSTALL) -m 664 -D $(SOLO5_OBJ) $@

$(RUMP_BAKE): $(RUMP_SOLO5_X86_64)

$(NODEJS_BIN): $(RUMPCONFIG) rumprun
	/bin/bash -c "source $(RUMP_DIR)/obj/config-PATH.sh && $(MAKE) -C $(NODEJS_PATH)"

$(RUMPCONFIG):
	echo "RUMPRUN_TOOLCHAIN_TUPLE=x86_64-rumprun-netbsd" > $@

ext-clean:
	$(MAKE) -C $(SOLO5_DIR) clean
	$(MAKE) -C $(RUMP_DIR) clean
	$(MAKE) -C $(NODEJS_PATH) clean

.PHONY: ext-clean solo5 rumprun nodejs
