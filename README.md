Umm is a implementation/remix of the Om execution model developed by the PSML Research Group at Boston University.

# Umm…
Unikernel Monitor Management library for the EbbRT native runtime.

Umm enables the execution of unikernel monitors (`Um`) in a multicore baremetal runtime environment (e.g., kvm-qemu).

## Build
#### Requires the EbbRT toolchain

```$ git clone --recursive <this-repo>```

```$ make -C tests/simple_test```

## Run 
#### Requires kvm-qemu

```$ make -C tests/simple_test run ```
