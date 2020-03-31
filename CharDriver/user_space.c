#include "adc8.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>              /* open */
#include <unistd.h>             /* exit */
#include <sys/ioctl.h>          /* ioctl */


#define DEVICE_FILE_NAME  "/dev/adc8"
//MODULE_LICENSE("GPL");

   uint16_t  ad_val=0;

int ioctl_set_channel(int file_desc,int channel)
{
    //int i;
    int c=0;
   
     c=ioctl(file_desc,IOCTL_CHANNEL_SET,channel);
    if(c<0)
    { printf("\n The channel selction has failed");
    }
    printf("\n The channel selction is succesful");

return 0;
}

int ioctl_set_alignment(int file_desc,char align)
{
    int c=0;
    c=ioctl(file_desc,IOCTL_ALIGNMENT_SET,align);
    if(c<0)
    { printf("\n The alignment has failed");
    }
    printf("\n  The alignment is succesful");
return 0;
}
         
/*int ioctl_set_shift(int file_desc,int shift)  
{
    int c=0;
    c=ioctl(file_desc,IOCTL_CHANNEL_SHIFT,shift);
    if(c<0)
    { printf("/n The alignment has failed:");
    }
    printf("/n The shift has been set:");
return 0;

}*/

/*
 * Main - Call the ioctl functions
 */
int main()
{ 
 
    int file_desc, ioc_channel,read_val,shift;
    //char value="Y"; 
    char align;
    int x,y;
    file_desc = open(DEVICE_FILE_NAME,0);
    if (file_desc < 0)
    {
        printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
        exit(-1);
    }
     printf("\n The driver has been opened");   
     
   printf("\n Select the Channel(0-7) from which the ADC has to be read :");
   scanf("%d", &ioc_channel);
   printf("\n Select the alignment of the adc value L for Left and R for right : ");
   scanf(" %c", &align);
   //printf("\n values read are %d %c",ioc_channel,align);
   //printf("/n By how many digits should the final value be shifted:");
   //scanf("%d",&shift);   
   
   ioctl_set_channel(file_desc,ioc_channel);
   ioctl_set_alignment(file_desc,align);
   //ioctl_set_shift(file_desc,shift);
   
   read_val=read(file_desc,&ad_val,sizeof(ad_val));
   
   printf("\n The aligned adc value read by the driver: 0x%x \n",ad_val);
   close(file_desc);
   return 0;
}
