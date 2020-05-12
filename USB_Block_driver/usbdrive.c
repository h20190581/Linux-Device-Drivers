#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include<linux/slab.h>
#include <stdarg.h>
#include<linux/blkdev.h>
#include<linux/genhd.h>
#include<linux/spinlock.h>
#include<linux/init.h>
#include <linux/workqueue.h>

//----------------------------------------------------Block device macros ---------------------------------------------------
#define DEVICE_NAME "SD_CARD"
#define NR_OF_SECTORS 121896960
#define SECTOR_SIZE 512
#define CARD_CAPACITY  (NR_OF_SECTORS*SECTOR_SIZE)
#define MAJOR_NO 166
#define MINORS 2
#define FIRST_MINOR 0

//--------------------------------------------------List of all types of SCSI commands------------------------------------------/
#define SCSI_TEST_UNIT_READY            0x00
#define SCSI_REQUEST_SENSE              0x03
#define SCSI_FORMAT_UNIT                0x04
#define SCSI_INQUIRY                    0x12
#define SCSI_MODE_SELECT6               0x15
#define SCSI_MODE_SENSE6                0x1A
#define SCSI_START_STOP_UNIT            0x1B
#define SCSI_MEDIA_REMOVAL              0x1E
#define SCSI_READ_FORMAT_CAPACITIES     0x23
#define SCSI_READ_CAPACITY              0x25
#define SCSI_READ10                     0x28
#define SCSI_WRITE10                    0x2A
#define SCSI_VERIFY10                   0x2F
#define SCSI_MODE_SELECT10              0x55
#define SCSI_MODE_SENSE10               0x5A
//-----------------------------------------------------List of devices ----------------------------------
#define SANDISK_VID 0x0781
#define SANDISK_PID 0x5567

#define KINGSTON_VID 0x0930
#define KINGSTON_PID 0x6545

#define SONY_STORAGE_MEDIA_VID 0x054C
#define SONY_STORAGE_MEDIA_PID 0x0439

#define TRANSCEND_VID 0x0457
#define TRANSCEND_PID 0x0151

#define REQUEST_RESET   0x21
#define REQUEST_NO      0xFF

#define LUN_REQUEST_TYPE  0xA1
#define LUN_REQUEST_NO    0xFE

#define BULK_OUT_ENDPOINT 0X02
#define BULK_IN_ENDPOINT 0x81

#define READ_CAPACITY_LENGTH  0x08
#define USB_ERROR_PIPE  9
#define RETRY_MAX		5
#define USB_SUCCESS 0

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])
uint32_t expected_tag;
int major;
sector_t start_sector;
sector_t xfer_sectors;
unsigned char* buffer = NULL;
unsigned int offset;
size_t xfer_len;
int size;
unsigned int pipe;

struct my_work{
	struct work_struct work;
	void *data;
}my_work;

static struct workqueue_struct *myqueue = NULL;

static struct request_queue *q=NULL;


//----------------------------------------------------------------USB_Device_id table---------------------------------//
static const struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(SANDISK_VID,SANDISK_PID)},
	{USB_DEVICE(KINGSTON_VID,KINGSTON_PID)},
	{USB_DEVICE(TRANSCEND_VID,TRANSCEND_PID)},
	{USB_DEVICE(SONY_STORAGE_MEDIA_VID,SONY_STORAGE_MEDIA_PID)},
	{}/*terminating entry*/
};

//----------------------------------------------------------------Block Device Structure--------------------------------//
//BLock Operations structure//

struct my_block_struct{
	struct request_queue *queue;
	struct gendisk *gd;
	spinlock_t lock;
};

static struct my_block_struct  *device = NULL;

static struct block_device_operations my_block_ops ={
  .owner = THIS_MODULE
};
//----------------------------------------------------------------USB assocaited structures---------------------------------//
/*USB Operations structure*/
	static struct usb_device *udev;
//  Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Command Status Wrapper (CSW)
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

//-------------------------------------------------------------------------------------------------------------------------//



static void usbdev_disconnect(struct usb_interface *interface)
{
  blk_cleanup_queue(device->queue);
  del_gendisk(device->gd);
  unregister_blkdev(major,DEVICE_NAME);
  kfree(device);
  printk(KERN_INFO"Block device unregistered successfully\n");
  printk(KERN_INFO"device disconnected\n");
	printk(KERN_INFO "USBDEV device Removed\n");
	return;
}

//----------------------------------------------Send Mass Storagr Command-------------------------------------/

static int send_mass_storage_command(struct usb_device *udev, uint8_t lun,	uint8_t *cdb, uint8_t direction, uint8_t endpoint,size_t data_length,uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i,r, size;

	typedef struct command_block_wrapper command_block_wrapper;
	command_block_wrapper *cbw;
	cbw = (command_block_wrapper *) kzalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);

	if (cdb == NULL) {
		return -1;
	}
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
	cbw->bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
		memcpy(cbw->CBWCB, cdb, cdb_len);
		i=0;
  do {
  		r = usb_bulk_msg(udev,usb_sndbulkpipe(udev,endpoint), (void*)cbw, 31, &size,1000);
     i++;
	} while((r!=0)&&(i<2));
		//r = usb_bulk_msg(udev,usb_sndbulkpipe(udev,direction), (unsigned char*)cbw, 31, &size,0);
		if (r == USB_ERROR_PIPE) {
			printk(KERN_INFO "Pipe Error :%d",r);
			goto error;
		}

	if (r != USB_SUCCESS) {
		printk(KERN_INFO " send_mass_storage_command is not a success : %d\n",r);
		goto error;
	}

	printk(KERN_INFO" sent %d CDB bytes\n", cdb_len);
	//kfree(cbw);
	return 0;

	error:
				kfree(udev);
				return -1;
}

//-----------------------------------------------------------------Get Mass Storage Status ----------------------------------------------------//
static int get_mass_storage_status(struct usb_device *udev ,uint32_t expected_tag)
{
	int  i,r, size;
	typedef struct command_status_wrapper command_status_wrapper;
	command_status_wrapper *csw;

	csw = ( command_status_wrapper *) kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);
	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
		i=0;
   do{
		r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev,BULK_IN_ENDPOINT), (void*)csw, 13, &size,1000);
    i++;
	}while((r!=0)&&(i<5));


  printk(KERN_INFO "Size: %ud",size);

 	printk(KERN_INFO " Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
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


	if (csw->bCSWStatus) {

		if (csw->bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	return 0;

}

//---------------------------------------------------------Deferred Work----------------------------------------------/
void defered_work (struct work_struct *work)
{
	struct my_work *mwp = container_of(work, struct my_work, work); //pointer to my_work structure to point to the starting of my_work structure
	struct request *rq;

	struct req_iterator iter;
	struct bio_vec bvec;
	struct command_block_wrapper *cbw;

	uint32_t expected_tag;
	int j, size;
	int r = 0;
  rq=mwp->data;

	printk(KERN_ALERT "Inside defered_work function\n");
	printk(KERN_ALERT "Target Sector No. %lu ", rq->__sector);
	rq_for_each_segment(bvec, rq, iter)
	{
		int dir;
		start_sector = iter.iter.bi_sector;
		buffer = kmap_atomic(bvec.bv_page);
		offset = bvec.bv_offset;
		xfer_len = bvec.bv_len;
		xfer_sectors = xfer_len/ SECTOR_SIZE;
		dir = rq_data_dir(rq);
		printk(KERN_INFO "len = %lu, sectors = %lu\n", xfer_len, xfer_sectors);
		printk(KERN_INFO "start_sector = %lu, dir = %d\n", start_sector,dir);
		printk(KERN_INFO "buffer = %d, offset = %u\n", buffer, offset);
		cbw = (struct command_block_wrapper *)kzalloc(sizeof(struct command_block_wrapper), GFP_KERNEL);
		//memset(cdb, 0, sizeof(cdb));
		//memset(cbw, 0, sizeof(struct command_block_wrapper));

		cbw->dCBWSignature[0] = 'U';
		cbw->dCBWSignature[1] = 'S';
		cbw->dCBWSignature[2] = 'B';
		cbw->dCBWSignature[3] = 'C';
		cbw->bCBWLUN = 0x00;
		//cbw->bCBWCBLength = 0x10;

							cbw->CBWCB[2] = (start_sector >> 24) & 0xFF ;
							cbw->CBWCB[3] = (start_sector >> 16) & 0xFF ;
							cbw->CBWCB[4] = (start_sector >> 8) & 0xFF ;
							cbw->CBWCB[5] = (start_sector >> 0) & 0xFF ;
							cbw->CBWCB[7] = (xfer_sectors >> 8) & 0xFF ;
							cbw->CBWCB[8] = (xfer_sectors >> 0) & 0xFF ;

		if(dir == 1) //For a write request
		{
		//	pipe = usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT);
			cbw->dCBWDataTransferLength = xfer_len;
			cbw->bmCBWFlags = 0x00;
			cbw->CBWCB[0] = SCSI_WRITE10;
			cbw->bCBWCBLength = cdb_length[cbw->CBWCB[0]];
			cbw->dCBWTag = 0x0005;
			expected_tag = 0x0005;
			j = 0;
			do
			{
			//The transfer length must always be exactly 31 bytes.
				r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT), (void *)cbw, 31, &size, 1000);
				if (r != 0) {
					usb_clear_halt(udev,usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT));
					printk(KERN_INFO "cbw out for write failed\n");
					}
				j++;
			} while ((r != 0) && (j<RETRY_MAX));

			printk(KERN_INFO "read count in write = %d\n", size);
			r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT), (void *) (buffer+offset), xfer_len, &size, 5000);
			if(r != 0)
			{
				printk(KERN_INFO "writing into drive failed\n");
				usb_clear_halt(udev, usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT));
			}
			printk(KERN_INFO"Write is completed");
			r=get_mass_storage_status(udev,expected_tag);
			//kunmap_atomic(buffer);
		}
		else  // For a read request
		{
			//pipe = usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT);
			cbw->dCBWDataTransferLength = xfer_len;
			cbw->bmCBWFlags = 0x80;
			cbw->CBWCB[0] = 0x28;
			cbw->bCBWCBLength = cdb_length[cbw->CBWCB[0]];
			cbw->dCBWTag = 0x0006;
			expected_tag = 0x0006;
			j = 0;
			do {
			// The transfer length must always be exactly 31 bytes.
				r = usb_bulk_msg(udev,  usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT), (void *)cbw, 31, &size, 1000);
				if (r != 0) {
					usb_clear_halt(udev, usb_sndbulkpipe(udev, BULK_OUT_ENDPOINT));
					printk(KERN_INFO "cbw out for read failed, j = %d\n", j);
				}
				j++;
			} while ((r != 0) && (j<RETRY_MAX));

			printk(KERN_INFO "read count = %d\n", size);
			r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_IN_ENDPOINT), ((void *)(buffer+offset)), xfer_len, &size, 5000);
			if(r != 0)
			{
				printk(KERN_INFO "reading from drive failed:%d\n",r);
				usb_clear_halt(udev, usb_rcvbulkpipe(udev, BULK_IN_ENDPOINT));
			}
			r=get_mass_storage_status(udev,expected_tag);

		}

		}

		 __blk_end_request_cur(rq, 0);
		kunmap_atomic(buffer);

		printk(KERN_INFO"Finished my block transfer\n");
		kfree(mwp);
		return;

}


//-------------------------------------- Block request function -----------------------------///
void block_request(struct request_queue* q)
{
	struct request *rq=NULL; //Request structure
	struct my_work *usb_work=NULL; //Pointer to my work structure

	printk(KERN_ALERT "Inside request function\n");

	while((rq = blk_fetch_request(q))!=NULL)  //fetch the requests until the queue
	{


		if(rq == NULL || blk_rq_is_passthrough(rq) == 1)//To check if if its file system request
		{
			printk(KERN_INFO "non FS request");
			__blk_end_request_all(rq, -EIO);
			continue;
		}
		

		usb_work=(struct my_work *)kmalloc(sizeof(struct my_work),GFP_ATOMIC);
		if(usb_work==NULL)
		{
			printk(KERN_INFO"Memory allocation for the work structure failed:\n");
			
			__blk_end_request_all(rq,0);
			continue;
		}

		printk("Deferring req =%p",rq);
		usb_work->data=rq;
		INIT_WORK(&usb_work->work,defered_work); //Defered_work function will be executed upon postponment
		queue_work(myqueue, &usb_work->work); // Queueing work in the work_queue
		//blkdev_dequeue_request(rq);

	}
	printk(KERN_INFO "Outside Request func loop:\n");
	return;
}


//-------------------------------------------------------USB Probe function--------------------------------------------------//
static int usbdev_probe(struct usb_interface* interface,const struct usb_device_id *id)
{
  	struct usb_host_interface *iface_desc;
		struct usb_endpoint_descriptor *endpoint;
		int i,size,r;
		uint32_t  max_lba, block_size;
		long long device_size,temp;
		uint8_t *cdb;  	// SCSI Command Descriptor Block

	  long long dev_size=0;
		uint8_t endpoint_in = 0, endpoint_out = 0;
			unsigned char epAddr, epAttr;

			printk(KERN_INFO "\n INSIDE PROBE FUNCTION");

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
		else if(id->idVendor == SANDISK_VID)
		{
			printk(KERN_INFO "Sandisc Plugged in\n");
			printk(KERN_INFO "Known USB detected\n");
		}
		else if(id->idVendor == KINGSTON_VID)
		{
			printk(KERN_INFO "Kingston Pendrive is  Plugged in\n");
			printk(KERN_INFO "Known USB detected\n");
		}
		else
		{
			printk(KERN_INFO"Unknown USB detected");
		}

		/* allocate memory for our device state and initialize it */
		udev = kzalloc(sizeof(struct usb_device), GFP_KERNEL);
		if (udev == NULL) {
			printk(KERN_INFO "Out of memory \n");
			goto error;
    }


		 udev = interface_to_usbdev(interface);


		printk(KERN_INFO "USB VID : %x", udev->descriptor.idVendor);
		printk(KERN_INFO "USB PID : %x", udev->descriptor.idProduct);
		printk(KERN_INFO "USB device CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
		printk(KERN_INFO "USB device SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
		printk(KERN_INFO "USB device Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);
		printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);

		/* set up the endpoint information */
		/* use only the first bulk-in and bulk-out endpoints */
	      	/* update settings only for bulk SCSI*/
		iface_desc = interface->cur_altsetting;

		for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
		{
			endpoint = &interface->cur_altsetting->endpoint[i].desc;
			epAddr = endpoint->bEndpointAddress;
			epAttr = endpoint->bmAttributes;

			if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
			{
				if(epAddr & 0x80)
					{
					printk(KERN_INFO "EP %d is Bulk IN\n", i);
					endpoint_in = epAddr;
					}
				else
					{
					printk(KERN_INFO "EP %d is Bulk OUT\n", i);
					endpoint_out= epAddr;
					}
		}
	}
//-------------------------------------------------Read Capacity Command -------------------------------------------//
	 printk("Reading Capacity:\n");
	 buffer=(uint8_t *)kmalloc(8*sizeof(uint8_t),GFP_KERNEL);

	 for(i=0;i<8;i++)
		 *(buffer+i)=0;

	 cdb= kmalloc(16*sizeof(uint8_t ),GFP_KERNEL);

	 for(i=0;i<16;i++)
			 *(cdb+i)=0;

	 *cdb = 0x25;	// Read Capacity

	 r=send_mass_storage_command(udev, 0, cdb,0x80, endpoint_out, READ_CAPACITY_LENGTH, &expected_tag);
	 if(r != 0) printk(KERN_INFO " CAPACITY REQUEST FAILED \n");
	 else{
		 r=usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint_in ), (void*)buffer, READ_CAPACITY_LENGTH, &size, 10*HZ);
		 if(r!=0) printk(KERN_INFO " CAPACITY RESPONSE FAILED \n");
		 else{

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
		 }
	 }

	 get_mass_storage_status(udev, expected_tag);

//-----------------------------------------------------Block Driver registration --------------------------------------//

   ///Blockn Driver Registration
	 		device=kmalloc(sizeof(struct my_block_struct),GFP_KERNEL);
		if( (major=register_blkdev(0,DEVICE_NAME))<0)
 		{
	 		printk(KERN_INFO"Unable to register block device : USB_disk\n");
	 		return -EBUSY;
 		}
		printk("Major_number:%d\n",major);


		device->gd = alloc_disk(MINORS);
  	if(!device->gd)
  	{
    	printk(KERN_INFO"Gendisk is not allocated\n");
    	unregister_blkdev(major,DEVICE_NAME);
    	kfree(device);
    	return -ENOMEM;
  	}
    printk("gendisk_allocated\n");
		strcpy(device->gd->disk_name,DEVICE_NAME);
  	device->gd->first_minor = FIRST_MINOR;
  	device->gd->major = major;
  	device->gd->fops = &my_block_ops;
  	spin_lock_init(&device->lock);
  	if(!(device->gd->queue = blk_init_queue(block_request,&device->lock)))
  	{
    	printk("Request_queue allocated failed\n");
    	del_gendisk(device->gd);
    	unregister_blkdev(major,DEVICE_NAME);
    	kfree(device);
    	return -ENOMEM;
  	}
		blk_queue_logical_block_size(device->gd->queue,SECTOR_SIZE);
  	device->queue = device->gd->queue;
//  device->gd->queue->queuedata = path;
  	set_capacity(device->gd,NR_OF_SECTORS);
  	device->gd->private_data = device;
  	add_disk(device->gd);
  	printk(KERN_INFO"Block device successfully registered\n");
		return 0;

error:

			kfree(udev);
			return 0;
}



static struct usb_driver usbudev_driver = {
	name: "my_USB dev",  //name of the device
	probe: usbdev_probe, // Whenever device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};


static int  device_init(void)
{
  printk(KERN_INFO "UAS SCSI supported USB driver in inserted :");
	usb_register(&usbudev_driver);
	myqueue = create_workqueue("myqueue");
	printk(KERN_INFO "\n Work queue initilaized");
	return 0;
}

static void  device_exit(void)
{
	flush_workqueue(myqueue);
	destroy_workqueue(myqueue);
  usb_deregister(&usbudev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
	//return 0;
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
