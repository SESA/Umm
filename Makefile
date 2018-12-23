#
# Primary Makefile for the Umm library and its dependencies
#
include Makefile.common

# Gotta be a better way.
UMM_SOURCE=$(wildcard $(SRCDIR)/*.cc)
UMM_SOURCE+=$(wildcard $(SRCDIR)/*.S)

UMM_OBJS_TMP=$(UMM_SOURCE:$(SRCDIR)/%.cc=$(BUILDDIR)/%.o)
UMM_OBJS=$(UMM_OBJS_TMP:$(SRCDIR)/%.S=$(BUILDDIR)/%.o)

all: build

$(BUILDDIR):
	$(MKDIR) $@

$(INSTALLDIR):
	$(MKDIR) $@

$(EBBRT_SYSROOT): # verify we have an EbbRT toolchain
	$(error EBBRT_SYSROOT is undefined and can not be located)

$(BUILDDIR)/libumm.a: $(UMM_OBJS) | $(BUILDDIR) 
	${EBBRTAR} ${UMM_ARFLAGS} $@ $(UMM_OBJS) 

$(BUILDDIR)/%.o: $(SRCDIR)/%.S | $(BUILDDIR)
	${EBBRTAS} $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cc | $(BUILDDIR)
	${EBBRTCXX} ${UMM_CPP_FLAGS} -c $< -o $@

build: $(BUILDDIR)/libumm.a

distclean: clean

clean: 
	-$(RM) $(BUILDDIR)/libumm.a	
	-${RM} $(wildcard $(BUILDDIR)/*.d $(BUILDDIR)/*.o)

.PHONY: all build install clean distclean umm
