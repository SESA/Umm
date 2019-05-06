-include ../../Makefile.common

build: target.binelf $(UMM_INSTALL_DIR)/libumm.a
	#${EBBRTCXX} ${UMM_CPP_FLAGS} -c checkpoint_test.cc -o checkpoint_test.o -I$(UMM_INCLUDE_DIR)
	#${EBBRTCXX} ${UMM_CPP_FLAGS} checkpoint_test.o target.binelf $(UMM_INSTALL_DIR)/libumm.a -T $(UMM_INCLUDE_DIR)/umm.lds -o checkpoint_test.elf
	${EBBRTCXX} ${UMM_CPP_FLAGS} -c python_server_test.cc -o python_server_test.o -I$(UMM_INCLUDE_DIR)
	${EBBRTCXX} ${UMM_CPP_FLAGS} python_server_test.o target.binelf $(UMM_INSTALL_DIR)/libumm.a -T $(UMM_INCLUDE_DIR)/umm.lds -o python_server_test.elf
	objcopy -O elf32-i386 python_server_test.elf python_server_test.elf32

-include ../../Makefile.targets


$(UMM_INSTALL_DIR)/libumm.a:
	$(MAKE) -C ../../

target.binelf: $(TARGET)
	$(USRDIR)/umm target

VM_CPU=4
VM_MEM=8G

run:
	NO_NETWORK=1 VM_CPU=$(VM_CPU) VM_MEM=$(VM_MEM) $(USRDIR)/launch.sh python_server_test.elf32

gdbrun:
	NO_NETWORK=1 GDB=1 VM_CPU=$(VM_CPU) VM_MEM=$(VM_MEM) $(USRDIR)/launch.sh python_server_test.elf32

clean:
	-$(RM) *.d *.elf *.elf32 *.binelf *.o target

.PHONY: build run gdbrun clean solo5-target
