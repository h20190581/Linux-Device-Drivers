#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include <linux/slab.h>
//#include<linux/scsi.h>

#define Sandisk_VID 0x0781
#define Sandisk_PID 0x5567

#define Kingston_VID 0x0930
#define Kingston_PID 0x6545

#define SONY_STORAGE_MEDIA_VID 0x054c
#define SONY_STORAGE_MEDIA_PID 0x0439

#define TRANSCEND_VID 0x0457
#define TRANSCEND_PID 0x0151

#define REQUEST_RESET   0x21
#define REQUEST_NO      0xFF

#define LUN_REQUEST_TYPE  0xA1
#define LUN_REQUEST_NO    0xfe

#define BULK_OUT_ENDPOINT 0X02
#define BULK_IN_ENDPOINT 0x81

#define READ_CAPACITY_LENGTH  0x08
#define USB_ERROR_PIPE  9
#define RETRY_MAX		5
#define USB_SUCCESS 0


#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])


// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

struct usb_skel {
	struct usb_device *	udev;			/* the usb device for this device */
	struct usb_interface *	interface;		/* the interface for this device */
	unsigned char *		bulk_in_buffer;		/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	struct kref		kref;
};

uint32_t expected_tag;

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(Sandisk_VID,Sandisk_PID)},
	{USB_DEVICE(Kingston_VID,Kingston_PID)},
	{USB_DEVICE(TRANSCEND_VID,TRANSCEND_PID)},
	{USB_DEVICE(SONY_STORAGE_MEDIA_VID,SONY_STORAGE_MEDIA_PID)},
	{}/*terminating entry*/
};

static int send_mass_storage_command(struct usb_skel *dev, uint8_t *lun,	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int r, size;
	struct command_block_wrapper *cbw;

	if (cdb == NULL) {
		return -1;
	}


  cbw=kmalloc(sizeof(cbw),GFP_KERNEL);
	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk(KERN_INFO "send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}



	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN = *lun;
	// Subclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);

		r = usb_bulk_msg(dev->udev,usb_sndbulkpipe(dev->udev,direction), (unsigned char*)cbw, 31, &size,0);
		if (r == USB_ERROR_PIPE) {
			printk(KERN_INFO "Pipe Error :%d",r);
			goto error;
		}

	if (r != USB_SUCCESS) {
		printk(KERN_INFO " send_mass_storage_command is not a success : %d\n",r);
		goto error;
	}

	printk(KERN_INFO" sent %d CDB bytes\n", cdb_len);
	return 0;

	error:
				kfree(dev);
				return -1;
}


static int get_mass_storage_status(struct usb_skel *dev, uint8_t endpoint, uint32_t expected_tag)
{
	int  r, size;
	struct command_status_wrapper *csw;

	csw=kmalloc(sizeof(csw),GFP_KERNEL);
	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
		r = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev,endpoint), (unsigned char*)csw, 13, &size, 1000);
		if (r == USB_ERROR_PIPE) {
			//libusb_clear_halt(handle, endpoint);
      goto error;
		}


	if (r != USB_SUCCESS) {
		printk(KERN_INFO "  get_mass_storage_status: %d\n",r);
		return -1;
	}
	if (size != 13) {
		printk(KERN_INFO " get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk(KERN_INFO "get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",	expected_tag, csw->dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printk(KERN_INFO " Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw->bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	return 0;

	error:
				kfree(dev);
				return -1;
}


static int test_mass_storage(struct usb_skel *dev,unsigned int endpoint_in,unsigned int endpoint_out)
{
	 int r, size;
		uint8_t *lun;
		uint32_t  max_lba, block_size;
		long long device_size,temp;
		uint8_t cdb[16];	// SCSI Command Descriptor Block
		uint8_t *buffer;
		long long dev_size=0;
		int i;
    // ##############################################################Bulk Only Mass Storage Reset######################################################
    printk(KERN_INFO "Implementing BOM Reset:");
		r=usb_control_msg(dev->udev,usb_rcvctrlpipe(dev->udev,0), REQUEST_NO ,REQUEST_RESET, 0, 0, NULL, 0, 0);
    if(r!=0)
		printk(KERN_INFO "\nCouldn't Reset the device");
    else
		printk(KERN_INFO "r =%d",r);

    //######################################################################LUN #######################################################################
	 printk(KERN_INFO "Reading Max LUN:\n");
   lun=(uint8_t*)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	 r = usb_control_msg(dev->udev,usb_rcvctrlpipe(dev->udev,0), LUN_REQUEST_NO ,LUN_REQUEST_TYPE, 0, 0, (void *)lun, 1, 0);


	if (r == 0) {
		lun = 0;
		printk(KERN_INFO "LUN: %d",*lun);
	} else if (r < 0) {
		printk(KERN_INFO "The Process Failed: %d",r);
		goto error;
	}
	printk(KERN_INFO "Max LUN = %d\n", *lun);


  //#####################################################################Read Capacity######################################################################
	// Read capacity
	printk("Reading Capacity:\n");
	//memset(buffer, 0, sizeof(buffer));
  buffer=(uint8_t *)kmalloc(8*sizeof(uint8_t),GFP_KERNEL);
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;	// Read Capacity

	send_mass_storage_command(dev, lun, cdb, endpoint_out, READ_CAPACITY_LENGTH, &expected_tag);
	usb_bulk_msg(dev->udev,usb_rcvbulkpipe(dev->udev,endpoint_in ), (unsigned char*)buffer, READ_CAPACITY_LENGTH, &size, 1000);
	printk(KERN_INFO "received %d bytes\n", size);
	max_lba = be_to_int32(buffer);
	block_size = be_to_int32(buffer+4);

  temp = ((long long)((max_lba+1))*block_size);
//Calcution for the size
   while((temp-1024)>=0)
   {
     temp=temp-1024;
		 dev_size++;
	 }

	temp=temp*1000/1024;

  printk(KERN_INFO "Size in kB:%lld.%lld",dev_size,temp);
  temp=dev_size;
	dev_size=0;
	while((temp-1024)>=0)
	{
		temp=temp-1024;
		dev_size++;
	}

  temp=temp*1000/1024;
	printk(KERN_INFO "Size in MB:%lld.%lld",dev_size,temp);
	temp=dev_size;
	dev_size=0;
	while((temp-1024)>=0)
	{
		temp=temp-1024;
		dev_size++;
	}
    temp=temp*1000/1024;
	printk(KERN_INFO "Size in GB:%lld.%lld",dev_size,temp);

	device_size = ((long long)((max_lba+1))*block_size/(1024*1024*1024));
	printk(KERN_INFO "Max LBA: %08X, Block Size: %08X ,Device Size:%lld.%lld Gb\n", max_lba, block_size,dev_size,temp);
  get_mass_storage_status(dev, endpoint_in, expected_tag);


 //####################################################################### Inquiry Command##############################################################
//   Inquiry
cdb[0] = 0x12;
cdb[4] = 0x24;
r= send_mass_storage_command(dev,lun,cdb, endpoint_out,0x24, &expected_tag);
//printk(KERN_INFO " status after cbw = %d\n", ret1);
buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);

	for(i=0;i<64;i++)
			*(buffer+i)=0;
if(r != 0)
printk(KERN_INFO "INQUIRY REQUEST FAILED \n");

else
{
r = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev,endpoint_in),(void *)buffer,64,&size,0);
kfree(buffer);

printk(KERN_INFO "INQUIRY RESPONSE \n");


if(r != 0)
printk(KERN_INFO "INQUIRY RESPONSE FAILED \n");
else
{
char vid[9], pid[9], rev[5];
for (i=0; i<8; i++) {
		vid[i] = *(buffer+8+i);
		pid[i] = *(buffer+16+i);
		rev[i/2] =*(buffer+32+i/2);
	}
	vid[8] = 0;
	pid[8] = 0;
	rev[4] = 0;
  printk(KERN_INFO "   VID:PID:REV \"%8s\":\"%8s\":\"%4s\"\n", vid, pid, rev);
}
}

	return 0;

	error:
				kfree(dev);
				return -1;
}


static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_skel *dev;

//###############################################################################List of known USB Devices ############################################33
	if(id->idProduct == TRANSCEND_PID)
	{
		printk(KERN_INFO "Transcend pendrive Plugged in\n");
		printk(KERN_INFO "Known USB detected\n");
	}
	else if(id->idProduct == SONY_STORAGE_MEDIA_PID)
	{
		printk(KERN_INFO "Sony pendrive Media Plugged in\n");
		printk(KERN_INFO "Known USB detected\n");
	}
	else if(id->idVendor == Sandisk_VID)
	{
		printk(KERN_INFO "Sandisc Plugged in\n");
		printk(KERN_INFO "Known USB detected\n");
	}
	else if(id->idVendor == Kingston_VID)
	{
		printk(KERN_INFO "Kingston Pendrive is  Plugged in\n");
		printk(KERN_INFO "Known USB detected\n");
	}

	//printk(KERN_INFO "Check point met:");
	dev = kzalloc(sizeof(struct usb_skel), GFP_KERNEL);
	if (!dev) {
		printk("Out of memory");
		goto error;
	}
	kref_init(&dev->kref);



    dev->udev=usb_get_dev(interface_to_usbdev(interface));
    dev->interface=interface;

		if((interface->cur_altsetting->desc.bInterfaceClass==8)&&(interface->cur_altsetting->desc.bInterfaceSubClass==6)){
			printk(KERN_INFO "USB attached valid SCSI device is plugged in");

			}
		else if((interface->cur_altsetting->desc.bInterfaceClass==8)&&(interface->cur_altsetting->desc.bInterfaceSubClass==!6))
		{
			printk(KERN_INFO "USB attached invalid SCSI device");
			goto error;
		}
		else if((interface->cur_altsetting->desc.bInterfaceClass==!8))
		{
			printk(KERN_INFO "Attached device is not USB Compatible");
			goto error;
		}

		printk(KERN_INFO "Opening device %04X:%04X...\n",dev->udev->descriptor.idVendor,dev->udev->descriptor.idProduct);



	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;

		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
			if(epAddr & 0x80){
			dev->bulk_in_endpointAddr=epAddr;
			printk(KERN_INFO "EP %d is Bulk IN\n", i);
			printk(KERN_INFO "EP  Bulk IN address is: %X\n",dev->bulk_in_endpointAddr);
			}
			else{
      dev->bulk_out_endpointAddr=epAddr;
			printk(KERN_INFO "EP %d is Bulk OUT\n", i);
			printk(KERN_INFO "EP  Bulk out address is: %X\n",dev->bulk_out_endpointAddr);
      }
   	}
  }

	//this line causing error
	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);




 test_mass_storage(dev,dev->bulk_in_endpointAddr,dev->bulk_out_endpointAddr);


//kfree(dev);
return 0;

error:
			kfree(dev);
			return 0;
}



/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "usbdev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};


int device_init(void)
{
  printk(KERN_INFO "UAS SCSI supported USB driver in inserted :");
	usb_register(&usbdev_driver);
	return 0;
}

void device_exit(void)
{
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
	//return 0;
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
