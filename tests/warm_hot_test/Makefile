-include ../../Makefile.common

build: target.binelf $(UMM_INSTALL_DIR)/libumm.a
	${EBBRTCXX} ${UMM_CPP_FLAGS} -c warm_hot_test.cc -o warm_hot_test.o -I$(UMM_INCLUDE_DIR)
	${EBBRTCXX} ${UMM_CPP_FLAGS} warm_hot_test.o target.binelf $(UMM_INSTALL_DIR)/libumm.a -T $(UMM_INCLUDE_DIR)/umm.lds -o warm_hot_test.elf
	objcopy -O elf32-i386 warm_hot_test.elf warm_hot_test.elf32

-include ../../Makefile.targets

$(UMM_INSTALL_DIR)/libumm.a:
	$(MAKE) -C ../../

target.binelf: $(TARGET)
	$(USRDIR)/umm target

run:
	LAUNCHER="taskset -c 1,3,5,7" NO_NETWORK=1 VM_CPU=4 VM_MEM=8G $(USRDIR)/launch.sh warm_hot_test.elf32

gdbrun:
	NO_NETWORK=1 GDB=1 VM_CPU=4 VM_MEM=8G $(USRDIR)/launch.sh warm_hot_test.elf32

clean:
	-$(RM) *.d *.elf *.elf32 *.binelf *.o target

.PHONY: build run gdbrun clean solo5-target
