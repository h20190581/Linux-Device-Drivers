#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/random.h>

#include "adc8.h"
 
static dev_t first; // variable for device number
static struct cdev c_dev; // variable for the character device structure
static struct class *cls; // variable for the device class

/*****************************************************************************
STEP 4 as discussed in the lecture, 
my_close(), my_open(), my_read(), my_write() functions are defined here
these functions will be called for close, open, read and write system calls respectively. 
*****************************************************************************/
int temp;
//int shift;
char chan;
static int my_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "Inside the kernel module \n");
	return 0;
}

static int my_close(struct inode *i, struct file *f)
{
	printk(KERN_INFO "Time to say goodbye to kernel module,back to userspace\n");
	return 0;
}

int rand_gen(void)    // callback function.
{
    uint16_t  rand;
    get_random_bytes(&rand, sizeof(rand));
    printk(KERN_INFO "Before Masking 0x%x",rand);
    rand&=0x03ff;
    printk(KERN_INFO "after masking 0x%x",rand);
    return rand;
}

int shift_gen(uint16_t x)
{
    
	if(chan=='R')	
	 x=x;
	else if (chan=='L')
	x=x<<6;	
       

	return x;
}


static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	uint16_t adc_val;
        adc_val=rand_gen();
	adc_val=shift_gen(adc_val);
	printk(KERN_INFO"\n the adc_val after alignment : 0x%x",adc_val);
	copy_to_user(buf,&adc_val,sizeof(adc_val));	
	

	return 0;
}



long device_ioctl(struct file *file,             
                  unsigned int ioctl_num,unsigned long ioctl_param)   
                  
{  
    
   
    switch (ioctl_num) {
    case IOCTL_CHANNEL_SET:
        temp=ioctl_param;
        printk(KERN_INFO "\n Reading from  channel %d",temp);
        break;
    case IOCTL_ALIGNMENT_SET:
        chan= (char)ioctl_param;
        if(chan=='R')
 	printk(KERN_INFO"\n The alignment set is RIGHT "); 
	else 
	printk(KERN_INFO"\n The alignment set is LEFT ");
	break; 
    
 default: break;

    }

    return 0;
}





//###########################################################################################


static struct file_operations fops =
{
  .owner 	= THIS_MODULE,
  .open 	= my_open,
  .release 	= my_close,
  .read 	= my_read,
  .unlocked_ioctl = device_ioctl,
};
 
//########## INITIALIZATION FUNCTION ##################
// STEP 1,2 & 3 are to be executed in this function ### 
static int __init mychar_init(void) 
{
	printk(KERN_INFO "Hola: adc driver registered");
	
	// STEP 1 : reserve <major, minor>
	if (alloc_chrdev_region(&first, 0, 1, "BITS-PILANI") < 0)
	{
		return -1;
	}
	
	// STEP 2 : dynamically create device node in /dev directory
    if ((cls = class_create(THIS_MODULE, "chardrv")) == NULL)
	{
		unregister_chrdev_region(first, 1);
		return -1;
	}
    if (device_create(cls, NULL, first, NULL, "adc8") == NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(first, 1);
		return -1;
	}
	
	// STEP 3 : Link fops and cdev to device node
    cdev_init(&c_dev, &fops);
    if (cdev_add(&c_dev, first, 1) == -1)
	{
		device_destroy(cls, first);
		class_destroy(cls);
		unregister_chrdev_region(first, 1);
		return -1;
	}
	return 0;
}
 
static void __exit mychar_exit(void) 
{
	cdev_del(&c_dev);
	device_destroy(cls, first);
	class_destroy(cls);
	unregister_chrdev_region(first, 1);
	printk(KERN_INFO "Bye: adc driver unregistered\n\n");
}
 
module_init(mychar_init);
module_exit(mychar_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sudharshan");
MODULE_DESCRIPTION("ADC Driver");
