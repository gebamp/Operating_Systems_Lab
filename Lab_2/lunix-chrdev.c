/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * < Your name George_Bampilis>
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;

/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	
	// The following line is used in case of error to print the contents of the
	// registers and the stack trace 
	WARN_ON ( !(sensor = state->sensor));
	/* Returns true  if  the last update value of the of the sensor last update time
	   was larger than the last recorded timestamp of buffer update if true then 
	   update sensor measurements
	 */
    return sensor->msr_data[state->type]->last_update > state->buf_timestamp;

}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor = state->sensor;
	unsigned long flags;
    uint32_t value;
    long tmp;
	
	debug("leaving\n");

	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */
	 // With this command we disable the interrupts before taking the spinlock
	 // The previous interupt state is stored in flags
	    spin_lock_irqsave(&sensor->lock, flags);

	/* Why use spinlocks? See LDD3, p. 119 
	Spinlocks are used when we are writing code that should not sleep for example intr_handlers
	since if we sleep there someone might never wake us up 
	Spinlocks  are faster also no context_switch is required */

	/*
	 * Any new data available?
	 */
	 // If there is no new data return(only true if retval is zero)
	if (!lunix_chrdev_state_needs_refresh(state)) {
        spin_unlock_irqrestore(&sensor->lock, flags);
        return -EAGAIN;
    }
	//Update timestamp of buffer
	state->buf_timestamp = sensor->msr_data[state->type]->last_update;
    // Gets the value of the sensor
	value = sensor->msr_data[state->type]->values[0];
	// Releases the lock and restores interupt state
	// from flags 
    spin_unlock_irqrestore(&sensor->lock, flags);
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
	// Based on the  the type  of the sensor 
	// we lookup the  conected recorded value 
	// the reason we use lookups is because 
	// we shouldnt do  floating point arithmetic
	// in kernel_space 
	    switch (state->type) {
    case BATT:
        tmp = lookup_voltage[value];
        break;
    case TEMP:
        tmp = lookup_temperature[value];
        break;
    case LIGHT:
        tmp = lookup_light[value];
        break;
    }
	// Updates the buffer and prints  from the data_buffer , maximum buffer size
	// character decimal,decimal if tmp >= 0 print "empty" else print "-"
	// print in tmp/1000 ,  tmp%1000 format  
	state->buf_lim = snprintf(state->buf_data, LUNIX_CHRDEV_BUFSZ, "%c%ld.%03ld\n", tmp >= 0 ? ' ' : '-', tmp / 1000, tmp % 1000);

	debug("leaving\n");
	return 0;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/
/*
Open method is provided for a driver to do any initialization in preparation 
for later operations Tasks that should be done in open:
1) Check device specific errors 
2) Initialize the device if it is being opened for the first time
3) Allocate and fill any ds to be put in filp->private-data 
Modus operandi: 
	1) Identify which device is opened  with the inode 
*/
static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
	/* ? */
	int ret;
	unsigned int minor_device_number;
	// Struct included in lunix_chrdev.h 
	struct lunix_chrdev_state_struct *state;

	debug("entering\n");
	ret = -ENODEV;
	// Set return value to no such device found error
	// if the following if statement is true no device was found 
	// you were not able to open the device  so just leave 
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;
	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */
	// Inode stores the minor device number 
	// we take this from system and store it in
	// a variable
	minor_device_number = iminor(inode);
	
	/* Allocate a new Lunix character device private state structure */
	// Standard method to allocate memory in kernel for a new  private state 
	// structure GFP_Kernel underlines that memory is allocated on behalf of user
	// and may sleep
	state = kmalloc(sizeof(struct lunix_chrdev_state_struct), GFP_KERNEL);
	// !of_pointer is equal to 0 which in c is considered as false
	   if (!state) {
        ret = -ENOMEM;
        goto out;
    }	
	//  From the 3 LSB  of the minor number of the inode we deduce the type of
	//  the sensor 
		state->type = minor_device_number & 7;
	// The rest of the bits  indicate the number of the sensor 
		state->sensor = &lunix_sensors[minor_device_number >> 3];

	// Set buf_timestap to 0 since this is the initialisation of the device
	   	state->buf_timestamp = 0;
	// Init semaphore to 1 in order for the first procces to grab it 
		sema_init(&state->lock, 1);
	// Private_data is set to null by open sys_call
	// we use this to preserve data across sys_calls 
		filp->private_data = state;
		debug("Data allocated\n");


out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	/* Free memory allocated for device */
	kfree(filp->private_data);
	return 0;
}

// Returns invalid  argument passed 
static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{   // if driver does not offer icotl
	// return -EINVAL
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
    ssize_t ret;

    struct lunix_sensor_struct *sensor;
    struct lunix_chrdev_state_struct *state;

    state = filp->private_data;
    WARN_ON(!state);

    sensor = state->sensor;
    WARN_ON(!sensor);;

	/* Lock? */
	/* If lock(semaphore) is already acquired by someone else then
	procces is put in the wait_queue. If semaphore acquired return 0
	and we dont get inside if  statement else enter if statement */

	/*Attempts to acquire the semaphore.If no more tasks are allowed to acquire the semaphore
	calling this function will put the task to sleep.If the sleep is interrupted by a signal, 
	this function will return -EINTR. If the semaphore is successfully acquired, this function returns 0.*/
    if (down_interruptible(&state->lock))
        return -ERESTARTSYS;
		/*Restartable syscall */
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
	/*f_pos is a long offset datatype  for seeking position
	if value is 0 then this  is a new measurement */
	    if (*f_pos == 0) {
        while (lunix_chrdev_state_update(state) == -EAGAIN) {
			  /* Release the semaphore since the proccess will sleep
			   later on with wait_even			
			*/
            up(&state->lock);
            if (filp->f_flags & O_NONBLOCK)
                return -EAGAIN;
            /* The process needs to sleep */
			/* The process needs to sleep until condition is evaluated
			or a signal is received. The conditions is checked everytime
			the woken que is  woken up returns 0 if condition to refresh is evaluated
			and erastsys interrupted wq is a quee waiting for procceses to be waken up
			when sensor is ready to deliver new datum*/
            if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state)))
                return -ERESTARTSYS;
			/*Start trying to acquire lock  */
            if (down_interruptible(&state->lock))
                return -ERESTARTSYS;
        }
    }

	/* End of file */
	if(*f_pos >= state->buf_lim){
		return 0;
	}	
	/* Determine the number of cached bytes to copy to userspace */
	/*If the size of requested input is larger than maximum possible
	  then set the count of bytes to read to max */
	if (cnt > state->buf_lim - *f_pos)
        cnt = state->buf_lim - *f_pos;
	/* Copy to user returns  to user space cnt amount of bytes
		usrbuf is the dst_addres in user space, second argument
		is  kernel_space if everything was written succesfully then 
		copy to user returs 0 and it does not enter the following if 
		statement */
    if (copy_to_user(usrbuf, state->buf_data + *f_pos, cnt)) {
        ret = -EFAULT;
        goto out;
    }
    *f_pos += cnt;
    ret = cnt;

	/* Auto-rewind on EOF mode? */
    if(*f_pos >= state->buf_lim){
		*f_pos = 0;
	}
out:
	/*Unlock*/
    up(&state->lock);
	
	return ret;
}

// Optional support for memory mmaped I/O
static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

// Orizetai diasundesi twn syscall me ta antistoixa methods entos tou s
// sugkekrimenou kernel module
static struct file_operations lunix_chrdev_fops = 
{
        .owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

// Method used to assign minor and major number to device 
int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
	
	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;
	
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	// register_chrdev_region :registers a range of device numbers 
	/* register_chrdev_region takes  3  attributes:
	1) First in desired range of device numbers (Major number)
	2) The total number of consecutive  numbers required
	3) The name of the device driver  */
	ret = register_chrdev_region(dev_no,lunix_minor_cnt,"lunix");
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}	
	/* ? */
	/* cdev_add: Adds a character device in the system 
	Takes 3 attributes: 
	1) First is the cdev structure for the device
	2) The first device number for which the device is responisble
	3) The number of consecutive minor numbers corresponding to this device */
	ret = cdev_add(&lunix_chrdev_cdev,dev_no,lunix_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
		
	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
