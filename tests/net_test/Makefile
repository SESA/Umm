-include ../../Makefile.common

build: target.binelf $(UMM_INSTALL_DIR)/libumm.a
	${EBBRTCXX} ${UMM_CPP_FLAGS} -c net_test.cc -o net_test.o -I$(UMM_INCLUDE_DIR)
	${EBBRTCXX} ${UMM_CPP_FLAGS} net_test.o target.binelf $(UMM_INSTALL_DIR)/libumm.a -T $(UMM_INCLUDE_DIR)/umm.lds -o net_test.elf
	objcopy -O elf32-i386 net_test.elf net_test.elf32

-include ../../Makefile.targets

$(UMM_INSTALL_DIR)/libumm.a:
	$(MAKE) -C ../../

target.binelf: $(TARGET)
	$(USRDIR)/umm target

run:
	NO_NETWORK=1 VM_CPU=4 VM_MEM=8G $(USRDIR)/launch.sh net_test.elf32

gdbrun:
	NO_NETWORK=1 GDB=1 VM_CPU=4 VM_MEM=8G $(USRDIR)/launch.sh net_test.elf32

clean:
	-$(RM) *.d *.elf *.elf32 *.binelf *.o target

.PHONY: build run gdbrun clean solo5-target
