/*
 * crypto-chrdev.c
 *
 * Implementation of character devices
 * for virtio-cryptodev device 
 *
 * Vangelis Koukis <vkoukis@cslab.ece.ntua.gr>
 * Dimitris Siakavaras <jimsiak@cslab.ece.ntua.gr>
 * Stefanos Gerangelos <sgerag@cslab.ece.ntua.gr>
 *
 */
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/wait.h>

#include "crypto-chrdev.h"
#include "crypto.h"
#include "debug.h"

#include "cryptodev.h"

/*
 * Global data
 */
struct cdev crypto_chrdev_cdev;

/**
 * Given the minor number of the inode return the crypto device 
 * that owns that number.
 **/
static struct crypto_device *get_crypto_dev_by_minor(unsigned int minor) {
    struct crypto_device *crdev;
    unsigned long flags;

    debug("Entering");

    spin_lock_irqsave(&crdrvdata.lock, flags);
    list_for_each_entry(crdev, &crdrvdata.devs, list) {
        if (crdev->minor == minor)
            goto out;
    }
    crdev = NULL;

out:
    spin_unlock_irqrestore(&crdrvdata.lock, flags);

    debug("Leaving");
    return crdev;
}

/*************************************
 * Implementation of file operations
 * for the Crypto character device
 *************************************/

static int crypto_chrdev_open(struct inode *inode, struct file *filp) {
    int ret = 0;
    int err;
    unsigned int len;
    struct crypto_open_file *crof;
    struct crypto_device *crdev;
    struct virtqueue *vq;
    struct scatterlist syscall_type_sg, host_fd_sg, *sgs[2];
    unsigned int num_out, num_in;
    unsigned int *syscall_type;
    int *host_fd;

    debug("Entering");

    syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
    *syscall_type = VIRTIO_CRYPTODEV_SYSCALL_OPEN;
    host_fd = kzalloc(sizeof(*host_fd), GFP_KERNEL);
    *host_fd = -1;

    num_out = 0;
    num_in = 0;

    if ((ret = nonseekable_open(inode, filp)) < 0)
        goto fail;

    /* Associate this open file with the relevant crypto device. */
    crdev = get_crypto_dev_by_minor(iminor(inode));
    if (!crdev) {
        debug("Could not find crypto device with %u minor", iminor(inode));
        ret = -ENODEV;
        goto fail;
    }
    vq = crdev->vq;

    crof = kzalloc(sizeof(*crof), GFP_KERNEL);
    if (!crof) {
        ret = -ENOMEM;
        goto fail;
    }
    crof->crdev = crdev;
    crof->host_fd = -1;
    filp->private_data = crof;

    /**
	 * We need two sg lists, one for syscall_type and one to get the 
	 * file descriptor from the host.
	 **/
    sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
    sgs[num_out++] = &syscall_type_sg;
    sg_init_one(&host_fd_sg, host_fd, sizeof(*host_fd));
    sgs[num_out + num_in++] = &host_fd_sg;

    /**
	 * Wait for the host to process our data.
	 **/
    down(&crdev->vq_sem);
    err = virtqueue_add_sgs(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC);
    virtqueue_kick(vq);
    while (virtqueue_get_buf(vq, &len) == NULL)
        /* do nothing */;
    up(&crdev->vq_sem);

    /* If host failed to open() return -ENODEV. */
    if (*host_fd < 0) {
        ret = -ENODEV;
        goto fail;
    }
    crof->host_fd = *host_fd;

fail:
    kfree(syscall_type);
    kfree(host_fd);
    debug("Leaving");
    return ret;
}

static int crypto_chrdev_release(struct inode *inode, struct file *filp) {
    int ret = 0;
    int err;
    unsigned int len;
    struct crypto_open_file *crof = filp->private_data;
    struct crypto_device *crdev = crof->crdev;
    struct virtqueue *vq = crdev->vq;
    struct scatterlist syscall_type_sg, host_fd_sg, *sgs[2];
    unsigned int num_out, num_in;
    unsigned int *syscall_type;
    int *host_fd;

    debug("Entering");

    syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
    *syscall_type = VIRTIO_CRYPTODEV_SYSCALL_CLOSE;
    host_fd = kzalloc(sizeof(*host_fd), GFP_KERNEL);
    *host_fd = crof->host_fd;

    num_out = 0;
    num_in = 0;

    /**
	 * Send data to the host.
	 **/
    sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
    sgs[num_out++] = &syscall_type_sg;
    sg_init_one(&host_fd_sg, host_fd, sizeof(*host_fd));
    sgs[num_out++] = &host_fd_sg;

    /**
	 * Wait for the host to process our data.
	 **/
    down(&crdev->vq_sem);
    err = virtqueue_add_sgs(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC);
    virtqueue_kick(vq);
    while (virtqueue_get_buf(vq, &len) == NULL)
        /* do nothing */;
    up(&crdev->vq_sem);

    kfree(syscall_type);
    kfree(host_fd);
    kfree(crof);
    debug("Leaving");
    return ret;
}

static long crypto_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    long ret = 0;
    int err;
    unsigned int len;
    struct crypto_open_file *crof = filp->private_data;
    struct crypto_device *crdev = crof->crdev;
    struct virtqueue *vq = crdev->vq;
    struct scatterlist syscall_type_sg, host_fd_sg, ioctl_cmd_sg, session_key_sg, session_op_sg, host_return_val_sg,
        ses_id_sg, crypt_op_sg, src_sg, iv_sg, dst_sg, *sgs[8];
    unsigned int num_out, num_in;
    unsigned int *syscall_type, *ioctl_cmd;
    int *host_fd, *host_return_val;
    unsigned char *session_key = NULL, *src = NULL, *iv = NULL, *dst = NULL;
    struct session_op *session_op = NULL;
    struct crypt_op *crypt_op = NULL;
    __u32 *ses_id = NULL;

    debug("Entering");

    /**
	 * Allocate all data that will be sent to the host.
	 **/
    syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
    *syscall_type = VIRTIO_CRYPTODEV_SYSCALL_IOCTL;
    host_fd = kzalloc(sizeof(*host_fd), GFP_KERNEL);
    *host_fd = crof->host_fd;
    ioctl_cmd = kzalloc(sizeof(*ioctl_cmd), GFP_KERNEL);
    *ioctl_cmd = cmd;
    host_return_val = kzalloc(sizeof(*host_return_val), GFP_KERNEL);

    num_out = 0;
    num_in = 0;

    /**
	 *  These are common to all ioctl commands.
	 **/
    sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
    sgs[num_out++] = &syscall_type_sg;
    sg_init_one(&host_fd_sg, host_fd, sizeof(*host_fd));
    sgs[num_out++] = &host_fd_sg;
    sg_init_one(&ioctl_cmd_sg, ioctl_cmd, sizeof(*ioctl_cmd));
    sgs[num_out++] = &ioctl_cmd_sg;

    /**
	 *  Add all the cmd specific sg lists.
	 **/
    switch (cmd) {
    case CIOCGSESSION:
        debug("CIOCGSESSION");
        session_op = kzalloc(sizeof(*session_op), GFP_KERNEL);
        if (copy_from_user(session_op, (void __user *)arg, sizeof(*session_op))) {
            ret = -EFAULT;
            goto fail;
        }
        session_key = kzalloc(session_op->keylen * sizeof(*session_key), GFP_KERNEL);
        if (copy_from_user(session_key, session_op->key, session_op->keylen * sizeof(*session_key))) {
            ret = -EFAULT;
            goto fail;
        }

        sg_init_one(&session_key_sg, session_key, session_op->keylen * sizeof(*session_key));
        sgs[num_out++] = &session_key_sg;
        sg_init_one(&session_op_sg, session_op, sizeof(*session_op));
        sgs[num_out + num_in++] = &session_op_sg;
        sg_init_one(&host_return_val_sg, host_return_val, sizeof(*host_return_val));
        sgs[num_out + num_in++] = &host_return_val_sg;

        break;

    case CIOCFSESSION:
        debug("CIOCFSESSION");
        ses_id = kzalloc(sizeof(*ses_id), GFP_KERNEL);
        if (copy_from_user(ses_id, (void __user *)arg, sizeof(*ses_id))) {
            ret = -EFAULT;
            goto fail;
        }

        sg_init_one(&ses_id_sg, ses_id, sizeof(*ses_id));
        sgs[num_out++] = &ses_id_sg;
        sg_init_one(&host_return_val_sg, host_return_val, sizeof(*host_return_val));
        sgs[num_out + num_in++] = &host_return_val_sg;

        break;

    case CIOCCRYPT:
        debug("CIOCCRYPT");
        crypt_op = kzalloc(sizeof(*crypt_op), GFP_KERNEL);
        if (copy_from_user(crypt_op, (void __user *)arg, sizeof(*crypt_op))) {
            ret = -EFAULT;
            goto fail;
        }
        src = kzalloc(crypt_op->len * sizeof(*src), GFP_KERNEL);
        if (copy_from_user(src, crypt_op->src, crypt_op->len * sizeof(*src))) {
            ret = -EFAULT;
            goto fail;
        }
        iv = kzalloc(VIRTIO_CRYPTODEV_BLOCK_SIZE * sizeof(*iv), GFP_KERNEL);
        if (copy_from_user(iv, crypt_op->iv, VIRTIO_CRYPTODEV_BLOCK_SIZE * sizeof(*iv))) {
            ret = -EFAULT;
            goto fail;
        }
        dst = kzalloc(crypt_op->len * sizeof(*dst), GFP_KERNEL);

        sg_init_one(&crypt_op_sg, crypt_op, sizeof(*crypt_op));
        sgs[num_out++] = &crypt_op_sg;
        sg_init_one(&src_sg, src, crypt_op->len * sizeof(*src));
        sgs[num_out++] = &src_sg;
        sg_init_one(&iv_sg, iv, VIRTIO_CRYPTODEV_BLOCK_SIZE * sizeof(*iv));
        sgs[num_out++] = &iv_sg;
        sg_init_one(&dst_sg, dst, crypt_op->len * sizeof(*dst));
        sgs[num_out + num_in++] = &dst_sg;
        sg_init_one(&host_return_val_sg, host_return_val, sizeof(*host_return_val));
        sgs[num_out + num_in++] = &host_return_val_sg;

        break;

    default:
        debug("Unsupported ioctl command");

        break;
    }

    /**
	 * Wait for the host to process our data.
	 **/
    down(&crdev->vq_sem);
    err = virtqueue_add_sgs(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC);
    virtqueue_kick(vq);
    while (virtqueue_get_buf(vq, &len) == NULL)
        /* do nothing */;
    up(&crdev->vq_sem);

    ret = *host_return_val;

    switch (cmd) {
    case CIOCGSESSION:
        if (copy_to_user((void __user *)arg, session_op, sizeof(*session_op))) {
            ret = -EFAULT;
            goto fail;
        }
        break;
    case CIOCFSESSION:
        break;
    case CIOCCRYPT:
        if (copy_to_user(((struct crypt_op *)arg)->dst, dst, crypt_op->len * sizeof(*dst))) {
            ret = -EFAULT;
            goto fail;
        }
        break;
    default:
        debug("Unsupported ioctl command");
        break;
    }

fail:
    kfree(syscall_type);
    kfree(host_fd);
    kfree(ioctl_cmd);
    kfree(host_return_val);

    switch (cmd) {
    case CIOCGSESSION:
        kfree(session_op);
        kfree(session_key);
        break;
    case CIOCFSESSION:
        kfree(ses_id);
        break;
    case CIOCCRYPT:
        kfree(crypt_op);
        kfree(src);
        kfree(iv);
        kfree(dst);
        break;
    default:
        debug("Unsupported ioctl command");
        break;
    }
    debug("Leaving");

    return ret;
}

static ssize_t crypto_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos) {
    debug("Entering");
    debug("Leaving");
    return -EINVAL;
}

static struct file_operations crypto_chrdev_fops =
    {
        .owner = THIS_MODULE,
        .open = crypto_chrdev_open,
        .release = crypto_chrdev_release,
        .read = crypto_chrdev_read,
        .unlocked_ioctl = crypto_chrdev_ioctl,
};

int crypto_chrdev_init(void) {
    int ret;
    dev_t dev_no;
    unsigned int crypto_minor_cnt = CRYPTO_NR_DEVICES;

    debug("Initializing character device...");
    cdev_init(&crypto_chrdev_cdev, &crypto_chrdev_fops);
    crypto_chrdev_cdev.owner = THIS_MODULE;

    dev_no = MKDEV(CRYPTO_CHRDEV_MAJOR, 0);
    ret = register_chrdev_region(dev_no, crypto_minor_cnt, "crypto_devs");
    if (ret < 0) {
        debug("failed to register region, ret = %d", ret);
        goto out;
    }
    ret = cdev_add(&crypto_chrdev_cdev, dev_no, crypto_minor_cnt);
    if (ret < 0) {
        debug("failed to add character device");
        goto out_with_chrdev_region;
    }

    debug("Completed successfully");
    return 0;

out_with_chrdev_region:
    unregister_chrdev_region(dev_no, crypto_minor_cnt);
out:
    return ret;
}

void crypto_chrdev_destroy(void) {
    dev_t dev_no;
    unsigned int crypto_minor_cnt = CRYPTO_NR_DEVICES;

    debug("entering");
    dev_no = MKDEV(CRYPTO_CHRDEV_MAJOR, 0);
    cdev_del(&crypto_chrdev_cdev);
    unregister_chrdev_region(dev_no, crypto_minor_cnt);
    debug("leaving");
}
