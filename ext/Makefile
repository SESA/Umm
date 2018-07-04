EXT_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# solo5
SOLO5_DIR ?= $(EXT_PATH)/solo5
SOLO5_OBJ ?= $(SOLO5_DIR)/kernel/ebbrt/solo5.o

# rumprun
RUMP_DIR ?= $(EXT_PATH)/rumprun
RUMP_BAKE = $(RUMP_DIR)/rumprun-solo5/bin/rump-bake
RUMP_SOLO5_X86_64=$(RUMP_DIR)/rumprun-solo5/rumprun-x86_64/lib
RUMP_SOLO5_UKVM=$(RUMP_SOLO5_X86_64)/rumprun-solo5/libsolo5_ukvm.a

# rumprun-packages
RUMPPACKDIR ?= $(EXT_PATH)/rumprun-packages
RUMPCONFIG = $(RUMPPACKDIR)/config.mk

#	rumprun-nodejs
NODEJS_PATH ?= $(RUMPPACKDIR)/nodejs/
NODEJS_BIN=$(NODEJS_PATH)/build-4.3.0/out/Release/node

$(SOLO5_OBJ):
	$(MAKE) -C $(SOLO5_DIR) ebbrt

$(RUMP_SOLO5_X86_64):
	cd $(RUMP_DIR) && git submodule update --init
	$(MAKE) -C $(RUMP_DIR) build 

$(RUMP_SOLO5_UKVM): $(SOLO5_OBJ) | $(RUMP_SOLO5_X86_64)
	$(INSTALL) -m 664 -D $(SOLO5_OBJ) $@

$(NODEJS_BIN): $(RUMPCONFIG)
	/bin/bash -c "source $(RUMP_DIR)/obj/config-PATH.sh && $(MAKE) -C $(NODEJS_PATH)"

$(RUMPCONFIG):
	echo "RUMPRUN_TOOLCHAIN_TUPLE=x86_64-rumprun-netbsd" > $@

solo5: $(SOLO5_OBJ)

rumprun: $(RUMP_SOLO5_X86_64)

nodejs: $(NODEJS_BIN)

ext-clean:
	$(MAKE) -C $(SOLO5_DIR) clean
	$(MAKE) -C $(RUMP_DIR) clean
	$(MAKE) -C $(NODEJS_PATH) clean

.PHONY: ext-clean solo5 rumprun nodejs
