// Code initially based on example from https://lwn.net/Articles/658511/

#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <sys/eventfd.h>
#include <unistd.h>

typedef struct {
    int kvmfd;
	int vmfd;
	int vcpufd;
	struct kvm_run *run;
	int vcpu_mmap_size;
	uint8_t *guest_mem;
	u_int guest_mem_size;
} vm_t;

static void handle_pmio(vm_t *vm) {
	struct kvm_run *run = vm->run;
    // Guest wrote to an I/O port
    if (run->io.direction == KVM_EXIT_IO_OUT) {
        uint8_t *addr = (uint8_t *)run + run->io.data_offset;
        switch (run->io.size) {
            case 1:  // retrieve the 8-bit value written by the guest
                printf("VMexit: PMIO port=0x%x value=%d\n", run->io.port, *addr);
                break;
            default:
                fprintf(stderr, "KVM_EXIT_IO: unsupported size\n");
        }
    }
}

static vm_t* vm_create() {
	vm_t *vm = malloc(sizeof(vm_t));
    if (!vm) err(1, NULL);
    memset(vm, 0, sizeof(vm_t));

    char kvm_dev[] = "/dev/kvm";
    vm->kvmfd = open(kvm_dev, O_RDWR | O_CLOEXEC);
    if (vm->kvmfd < 0) err(1, "%s", kvm_dev);

    // Make sure we have the right version of the API
    int version = ioctl(vm->kvmfd, KVM_GET_API_VERSION, NULL);
    if (version < 0) err(1, "KVM_GET_API_VERSION");
    if (version != KVM_API_VERSION) err(1, "KVM_GET_API_VERSION %d, expected %d", version, KVM_API_VERSION);

    vm->vmfd = ioctl(vm->kvmfd, KVM_CREATE_VM, 0);
    if (vm->vmfd < 0) err(1, "KVM_CREATE_VM");

    // Make sure we can manage guest physical memory slots
    if (!ioctl(vm->kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY))
    	errx(1, "Required extension KVM_CAP_USER_MEMORY not available");

    // Allocate 4KB of RAM for the guest
    vm->guest_mem_size = 4096;
    vm->guest_mem = mmap(NULL, vm->guest_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!vm->guest_mem) err(1, "allocating guest memory");

    uint8_t guest_code[] = {
        0xba, 0x23, 0x01, // mov dx,0x3f8
        0xb0, 42,         // mov al,123
        0xee,             // out dx,al
        0xf4,             // hlt
    };

    memcpy(vm->guest_mem, guest_code, sizeof(guest_code));

	// Map guest_mem to physical address 0 in the guest address space
    struct kvm_userspace_memory_region mem_region = {
        .slot = 0,
        .guest_phys_addr = 0,
        .memory_size = vm->guest_mem_size,
        .userspace_addr = (uint64_t)vm->guest_mem,
        .flags = 0
    };
    if (ioctl(vm->vmfd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) err(1, "KVM_SET_USER_MEMORY_REGION");

    // Create the vCPU
    vm->vcpufd = ioctl(vm->vmfd, KVM_CREATE_VCPU, 0);
    if (vm->vcpufd < 0) err(1, "KVM_CREATE_VCPU");

    // Setup memory for the vCPU
    vm->vcpu_mmap_size = ioctl(vm->kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
    if (vm->vcpu_mmap_size < 0) err(1, "KVM_GET_VCPU_MMAP_SIZE");

    if (vm->vcpu_mmap_size < (int)sizeof(struct kvm_run)) err(1, "KVM_GET_VCPU_MMAP_SIZE unexpectedly small");
    vm->run = mmap(NULL, (size_t)vm->vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vm->vcpufd, 0);
    if (!vm->run) err(1, "mmap vcpu");

    // Initialize CS to point to 0
    struct kvm_sregs sregs;
    if (ioctl(vm->vcpufd, KVM_GET_SREGS, &sregs) < 0) err(1, "KVM_GET_SREGS");
	sregs.cs.base = 0;
	sregs.cs.selector = 0;
	if (ioctl(vm->vcpufd, KVM_SET_SREGS, &sregs) < 0) err(1, "KVM_SET_SREGS");

    // Initialize instruction pointer and flags register
    struct kvm_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.rsp = vm->guest_mem_size;  // set stack pointer at the top of the guest's RAM
	regs.rip = 0;
	regs.rflags = 0x2;  // bit 1 is reserved and should always bet set to 1
    if (ioctl(vm->vcpufd, KVM_SET_REGS, &regs) < 0) err(1, "KVM_SET_REGS");

	return vm;
}

static void vm_run(vm_t *vm) {
    // Runs the VM (guest code) and handles VM exits
    while (1) {
		// Runs the vCPU until encoutering a VM_EXIT
        if (ioctl(vm->vcpufd, KVM_RUN, NULL) < 0) err(1, "KVM_RUN");
        switch (vm->run->exit_reason) {
            case KVM_EXIT_IO:    // encountered an I/O instruction
                handle_pmio(vm);
                break;
            case KVM_EXIT_HLT:   // encountered "hlt" instruction
                fprintf(stderr, "VMexit: KVM_EXIT_HLT\n");
				return;
            case KVM_EXIT_FAIL_ENTRY:
                fprintf(stderr, "VMexit: KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx\n",
                    (unsigned long long)vm->run->fail_entry.hardware_entry_failure_reason);
                break;
            case KVM_EXIT_INTERNAL_ERROR:
                fprintf(stderr, "VMexit: KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x\n", vm->run->internal.suberror);
				return;
			case KVM_EXIT_SHUTDOWN:
                fprintf(stderr, "VMexit: KVM_EXIT_SHUTDOWN\n");
				return;
            default:
                fprintf(stderr, "VMexit: unhandled exit reason (0x%x)\n", vm->run->exit_reason);
				return;
        }
    }
}

static void vm_destroy(vm_t *vm) {
    if (vm->guest_mem)
        munmap(vm->guest_mem, vm->guest_mem_size);
    if (vm->run)
        munmap(vm->run, vm->vcpu_mmap_size);
    close(vm->kvmfd);
    memset(vm, 0, sizeof(vm_t));
    free(vm);
}

int main() {
	vm_t *vm = vm_create();
	vm_run(vm);
	vm_destroy(vm);
    return EXIT_SUCCESS;
}
