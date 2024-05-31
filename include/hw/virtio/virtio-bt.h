/*
 * Virtio BT
 *
 */

#ifndef QEMU_VIRTIO_BT_H
#define QEMU_VIRTIO_BT_H

#include "hw/virtio/virtio.h"
#include "chardev/char-fe.h"
#include "standard-headers/linux/virtio_bt.h"
#include <sys/socket.h>

#define TYPE_VIRTIO_BT "virtio-bt-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOBT, VIRTIO_BT)
#define VIRTIO_BT_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_BT)

#define VIRTIO_BT_QUEUE_SIZE 1024

struct VirtIOBT {
    VirtIODevice parent_obj;
    int socket_fd;
    char *socket_path;
    VirtQueue *tx_queue;
    VirtQueue *rx_queue;
    QemuThread listen_thread;
    int thread_restart;
};

#endif