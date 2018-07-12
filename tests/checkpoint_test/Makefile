-include ../../Makefile.common

build: target.binelf $(UMM_INSTALL_DIR)/libumm.a
	${EBBRTCXX} ${UMM_CPP_FLAGS} -c checkpoint_test.cc -o checkpoint_test.o -I$(UMM_INCLUDE_DIR)
	${EBBRTCXX} ${UMM_CPP_FLAGS} checkpoint_test.o target.binelf $(UMM_INSTALL_DIR)/libumm.a -T $(UMM_INCLUDE_DIR)/umm.lds -o checkpoint_test.elf
	objcopy -O elf32-i386 checkpoint_test.elf checkpoint_test.elf32

-include ../Makefile.targets

TARGET=solo5-hello

$(UMM_INSTALL_DIR)/libumm.a:
	$(MAKE) -C ../../

target.binelf: $(TARGET)
	$(USRDIR)/umm target

VM_CPU=4
VM_MEM=8G

run:
	NO_NETWORK=1 VM_CPU=$(VM_CPU) VM_MEM=$(VM_MEM) $(USRDIR)/launch.sh checkpoint_test.elf32

gdbrun:
	NO_NETWORK=1 GDB=1 VM_CPU=$(VM_CPU) VM_MEM=$(VM_MEM) $(USRDIR)/launch.sh checkpoint_test.elf32

clean:
	-$(RM) *.d *.elf *.elf32 *.binelf *.o target

.PHONY: build run gdbrun clean solo5-target
