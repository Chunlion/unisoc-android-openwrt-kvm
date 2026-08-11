#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fail(const char *operation) {
    fprintf(stderr, "kvm-probe: %s: %s\n", operation, strerror(errno));
    return 1;
}

static int set_attr(int fd, unsigned group, unsigned long long attr, void *value) {
    struct kvm_device_attr device_attr = {
        .group = group,
        .attr = attr,
        .addr = (unsigned long long)value,
        .flags = 0,
    };
    return ioctl(fd, KVM_SET_DEVICE_ATTR, &device_attr);
}

int main(void) {
    int rc = 1;
    int vm = -1;
    int vgic = -1;
    int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm < 0)
        return fail("open /dev/kvm");
    if (ioctl(kvm, KVM_GET_API_VERSION, 0) != KVM_API_VERSION) {
        errno = EPROTO;
        goto out;
    }
    if (ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_DEVICE_CTRL) <= 0) {
        errno = ENOTSUP;
        goto out;
    }
    vm = ioctl(kvm, KVM_CREATE_VM, 0);
    if (vm < 0)
        goto out;

    struct kvm_create_device device = {
        .type = KVM_DEV_TYPE_ARM_VGIC_V3,
        .fd = 0,
        .flags = 0,
    };
    if (ioctl(vm, KVM_CREATE_DEVICE, &device) < 0)
        goto out;
    vgic = device.fd;

    unsigned long long redist = 0x3ffb0000ULL;
    unsigned long long dist = 0x3fff0000ULL;
    unsigned nr_irqs = 64;
    if (set_attr(vgic, KVM_DEV_ARM_VGIC_GRP_ADDR,
                 KVM_VGIC_V3_ADDR_TYPE_REDIST, &redist) < 0 ||
        set_attr(vgic, KVM_DEV_ARM_VGIC_GRP_ADDR,
                 KVM_VGIC_V3_ADDR_TYPE_DIST, &dist) < 0 ||
        set_attr(vgic, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0, &nr_irqs) < 0)
        goto out;
    rc = 0;

out:
    if (rc != 0)
        fail("KVM_CREATE_VM/VGICv3 capability check");
    if (vgic >= 0)
        close(vgic);
    if (vm >= 0)
        close(vm);
    close(kvm);
    return rc;
}
