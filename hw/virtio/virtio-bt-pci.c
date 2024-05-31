/*
 * Virtio bt PCI Bindings
 *
 * Copyright 2012 Red Hat, Inc.
 * Copyright 2012 Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/virtio/virtio-bt.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VirtIOBtPCI VirtIOBtPCI;

/*
 * virtio-bt-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_BT_PCI "virtio-bt-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOBtPCI, VIRTIO_BT_PCI,
                         TYPE_VIRTIO_BT_PCI)

struct VirtIOBtPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOBT vdev;
};

static void virtio_bt_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOBtPCI *vbt = VIRTIO_BT_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vbt->vdev);

    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }
}

static Property bt_properties[] = {
        /* DEFINE_PROP_CHR("chardev", VirtIOBT, chrdev), */
        DEFINE_PROP_END_OF_LIST(),
};

static void virtio_bt_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, bt_properties);

    k->realize = virtio_bt_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BT;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_bt_initfn(Object *obj)
{
    VirtIOBtPCI *dev = VIRTIO_BT_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_BT);
}

static const VirtioPCIDeviceTypeInfo virtio_bt_pci_info = {
    .base_name             = TYPE_VIRTIO_BT_PCI,
    .generic_name          = "virtio-bt-pci",
    .transitional_name     = "virtio-bt-pci-transitional",
    .non_transitional_name = "virtio-bt-pci-non-transitional",
    .instance_size = sizeof(VirtIOBtPCI),
    .instance_init = virtio_bt_initfn,
    .class_init    = virtio_bt_pci_class_init,
};

static void virtio_bt_pci_register(void)
{
    virtio_pci_types_register(&virtio_bt_pci_info);
}

type_init(virtio_bt_pci_register)