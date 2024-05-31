/*
 * A virtio device bt.
 *
 * Copyright 2019 Red Hat, Inc.
 * Copyright 2019 Yoni Bettan <ybettan@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "include/qemu/error-report.h"
#include "include/qemu/thread.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bt.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_bt.h"
#include "qapi/error.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

//pthread_mutex_t inbuf_mutex=PTHREAD_MUTEX_INITIALIZER;

/*
 * this function is called when the driver 'kick' the virtqueue.
 * since we can have more than 1 virtqueue we need the vq argument in order to
 * know which one was kicked by the driver.
*/

static void handle_rx(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void * handle_controller(void *args) {
    int msg_len, ret;
    char hci_frame[1000];
    VirtQueueElement *elem;
    VirtIOBT *vbt = args;

    for(;;) {
        memset(hci_frame, 0, 1000);
        msg_len = read(vbt->socket_fd, &hci_frame, 1000);
        if (msg_len < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            if (vbt->thread_restart > 0) {
                return 0;
            }
            continue;
        }
        if (msg_len < 0) {
            perror("Could not receive data from proxy");
            return NULL;
        }

        if (msg_len == 0) {
            error_report("Proxy closed connection");
            return NULL;
        }

        //info_report("Received data from Proxy");
        /* Send to driver */
        /* Wait for the lock, which is unlocked if a new writable buffer is added by the driver */
        while (!(elem = virtqueue_pop(vbt->rx_queue, sizeof(VirtQueueElement)))) {
            if (vbt->thread_restart > 0) {
                return 0;
            }
            usleep(1000);
        }
        if (vbt->thread_restart > 0) {
            return 0;
        }
        //info_report("Popped element from queue");

        ret = iov_from_buf(elem->in_sg, elem->in_num, 0, hci_frame, msg_len);
        if (ret != msg_len) {
            error_report("Size mismatch: Message is %d bytes, returned iov %d bytes", msg_len, ret);
            continue;
        }
        virtqueue_push(vbt->rx_queue, elem, msg_len);

        /* interrupt the driver */
        virtio_notify(&vbt->parent_obj, vbt->rx_queue);
        //info_report("Sent data to driver");
    }
    return NULL;
}

static void handle_tx(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    VirtIOBT *vbt = VIRTIO_BT(vdev);
    int size, ret;
    char* data;

    /*
     * get the virtqueue element sent from the driver.
     * in_sg are the driver inputs (device outputs)
     * out_sg are the driver output (device input) */
    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    size = iov_size(elem->out_sg, elem->out_num);

    if (size <= 0) {
        error_report("TXQueue: Empty VirtQueueElement");
        return;
    }

    data = malloc(size);

    /* read the driver output sg (device input sg) into a buffer */
    iov_to_buf(elem->out_sg, elem->out_num, 0, data, size);

    /* Send frame to socket */
    ret = send(vbt->socket_fd, data, size, MSG_EOR);
    if (ret < 0) {
        perror("Error sending to socket: ");
        return;
    }
    if (ret != size) {
        error_report("Wrote just %d bytes.", ret);
        return;
    }

    /* VirtIO Spec: 2.5 Virtqueues
     * Device executes the requests and - when complete - adds a used buffer to the queue -
     * i.e. lets the driver know by marking the buffer as used. Device can then trigger a
     * device event - i.e. send a used buffer notification to the driver. */
    virtqueue_push(vq, elem, 0);
    g_free(elem);
    virtio_notify(vdev, vq);

    return;
}

/*
 * This function gets the host features as a parameter and add to it all the
 * features supported by the device.
 * This bt-device has no currently defined feature bits but we still need
 * this function because when a device is plugged this function is called to
 * check the features offer by the device so it must exist and return the
 * host features without any change.
 */
static uint64_t
get_features(VirtIODevice *vdev, uint64_t features, Error **errp)
{
    virtio_add_feature(&features, VIRTIO_BT_F_VND_HCI);
    return features;
}

static void virtio_bt_get_config(VirtIODevice *vdev, uint8_t *config)
{
    /* Use static config yet */
    /* VirtIOBT *bt_dev = VIRTIO_BT(vdev); */
    struct virtio_bt_config btcfg = {.type = VIRTIO_BT_CONFIG_TYPE_PRIMARY, .vendor = VIRTIO_BT_CONFIG_VENDOR_NONE, .msft_opcode = 3};
    memcpy(config, &btcfg, sizeof(struct virtio_bt_config));
    return;
}

static void virtio_bt_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBT *vbt = VIRTIO_BT(dev);
    int ret;

    if(!vbt->socket_path) {
        error_setg(errp, "socket-path is not defined");
        return;
    }

    /* common virtio device initialization */
    virtio_init(vdev, VIRTIO_ID_BT, sizeof(struct virtio_bt_config));


    /* this device supports 2 virtqueue */
    vbt->tx_queue = virtio_add_queue(vdev, VIRTIO_BT_QUEUE_SIZE, handle_tx);
    vbt->rx_queue = virtio_add_queue(vdev, VIRTIO_BT_QUEUE_SIZE, handle_rx);

    /* Setup socket */
    vbt->socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un saddr = {AF_UNIX};

    /* Set timeout for read */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(vbt->socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    strncpy(saddr.sun_path, vbt->socket_path, sizeof(saddr.sun_path) - 1);
    ret = connect(vbt->socket_fd, (struct sockaddr*) &saddr, sizeof(saddr));
    if(ret < 0) {
        perror(NULL);
        error_setg(errp, "Cant connect to socket %s: %d", vbt->socket_path, errno);
        return;
    }

    vbt->thread_restart = 0;
    qemu_thread_create(&vbt->listen_thread, "virtio-bt-listen", &handle_controller, vbt, QEMU_THREAD_JOINABLE);
}

static void virtio_bt_device_unrealize(DeviceState *dev)
{
    error_report("virtio_bt: unrealize");

    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBT *vbt = VIRTIO_BT(dev);

    /* common virtio device cleanup */
    virtio_delete_queue(vbt->tx_queue);
    virtio_delete_queue(vbt->rx_queue);
    virtio_cleanup(vdev);
}

static int virtio_bt_post_load(void *opaque, int version_id) {
    VirtIOBT *vbt = VIRTIO_BT(opaque);

    vbt->thread_restart = 1;
    qemu_thread_join(&vbt->listen_thread);
    vbt->thread_restart = 0;
    qemu_thread_create(&vbt->listen_thread, "virtio-bt-listen", &handle_controller, vbt, QEMU_THREAD_JOINABLE);

    return 0;
}

static const VMStateDescription vmstate_virtio_bt = {
        .name = "virtio-bt",
        .fields = (VMStateField[]) {
                VMSTATE_VIRTIO_DEVICE,
                VMSTATE_END_OF_LIST()
        },
        .post_load = virtio_bt_post_load
};

static Property virtio_bt_properties[] = {
        DEFINE_PROP_STRING("socket-path", VirtIOBT, socket_path),
        DEFINE_PROP_END_OF_LIST(),
};


static void virtio_bt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_bt_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->vmsd = &vmstate_virtio_bt;
    vdc->realize = virtio_bt_device_realize;
    vdc->unrealize = virtio_bt_device_unrealize;
    vdc->get_features = get_features;
    vdc->get_config = virtio_bt_get_config;
}

static const TypeInfo virtio_bt_info = {
        .name = TYPE_VIRTIO_BT,
        .parent = TYPE_VIRTIO_DEVICE,
        .instance_size = sizeof(VirtIOBT),
        .class_init = virtio_bt_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_bt_info);
}

type_init(virtio_register_types)