#
# Primary Makefile for the Umm library and its dependencies
#
MYDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

-include config.mk

# Commands
CD ?= cd
CMAKE ?= cmake
INSTALL ?= install 
MAKE ?= make
MKDIR ?= mkdir -p

# Locations
BUILDDIR ?= $(MYDIR)/build
EXTDIR ?= $(MYDIR)/ext
INSTALLDIR ?= $(MYDIR)
SRCDIR ?= $(MYDIR)/src

# Umm
UMM_ARCH=x86_64
UMM_ARFLAGS =rcs 
UMM_CPPFLAGS = -MD -MP  # Why these?
UMM_OBJS=$(UMM_SOURCE:$(SRCDIR)/%.cc=$(BUILDDIR)/%.o)
UMM_SOURCE=$(wildcard $(SRCDIR)/*.cc)
UMM_CPP_FLAGS= -Wall -Werror 
ifdef DEBUG
UMM_CPP_FLAGS += -O0 -g3
endif

# EbbRT 
# 	NOTE: The EbbRT toolchain is required to build Umm
#   Set EBBRT_SYSROOT env variable (default: /opt/ebbrt)
EBBRT_SYSROOT ?= $(abspath /opt/ebbrt)
EBBRT_BIN_DIR=${EBBRT_SYSROOT}/usr/bin
EBBRTCC=${EBBRT_BIN_DIR}/x86_64-pc-ebbrt-gcc
EBBRTCXX=${EBBRT_BIN_DIR}/x86_64-pc-ebbrt-g++
EBBRTAR=${EBBRT_BIN_DIR}/x86_64-pc-ebbrt-ar

# TARGETS 
all: $(BUILDDIR)/libumm.a 

# Ext targets
-include $(EXTDIR)/Makefile

$(BUILDDIR):
	$(MKDIR) $@

$(INSTALLDIR):
	$(MKDIR) $@

$(EBBRT_SYSROOT): # verify we have an EbbRT toolchain
	$(error EBBRT_SYSROOT is undefined and can not be located)

$(BUILDDIR)/libumm.a: $(UMM_OBJS) | $(BUILDDIR) 
	${EBBRTAR} ${UMM_ARFLAGS} $@ $(UMM_OBJS) 

$(INSTALLDIR)/libumm.a: $(BUILDDIR)/libumm.a | $(INSTALLDIR)
	
$(BUILDDIR)/%.o: $(SRCDIR)/%.cc | $(BUILDDIR)
	${EBBRTCXX} ${UMM_CPPFLAGS} -std=gnu++14 ${UMM_CPP_FLAGS} -c $< -o $@

build: $(BUILDDIR)/libumm.a

install: $(INSTALLDIR)/libumm.a
	$(INSTALL) -m 664 -D $< $@

distclean: ext-clean clean

clean: 
	-$(RM) $(BUILDDIR)/libumm.a	
	-${RM} $(wildcard $(BUILDDIR)/*.d $(BUILDDIR)/*.o)

.PHONY: all build install clean distclean
