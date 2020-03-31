

#include <linux/ioctl.h>

#define MAJOR_NUM 120
#define IOCTL_CHANNEL_SET         _IOR(MAJOR_NUM,1,int )
#define IOCTL_ALIGNMENT_SET       _IOR(MAJOR_NUM,2, char )
#define IOCTL_CHANNEL_SHIFT       _IOR(MAJOR_NUM,3, int)


#define DEVICE_FILE_NAME "/dev/adc8"
