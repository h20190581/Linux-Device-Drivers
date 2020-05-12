This folder contains 2 files:
usbdrive.c (kercel C executable file)
Makefile(useful for compilation)

Steps for build
Move the above 2 mentioned files to folder and run the terminal
type "make all" for initiating the build
after the build insert the "usbdrive.ko" file generated to the kernel modules
make sure your Device named SD_CARD1 appears in the /dev directory

type "sudo fdisk -l"  to view the partition table entry.
mount the usbdevice using the "mount" cmd .
Mkae sure you are in root directory by typing "sudo -i".

Now mount the usb device in directory "/mnt/devices" to view the mounted contents in the pendrive.
For testing purposes create a .txt file in your USB device
Use vim [filename] to open the file for read/write op.

for writing:
type I to insert text to the file
press Esc to finish inserting
type :wq to save and close

Note:
1) unload all kernel usb modules such as uas usb_storage
2)Use a simple .txt file for verifying the functionality.



