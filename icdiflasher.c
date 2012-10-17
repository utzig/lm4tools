#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

// Flash Patch and Breakpoint: ARM Debug Interface V5 Architecture Specification
static const uint32_t FPB                0xe0002000

// Rom Control: see Stellaris LM4F120H5QR Microcontroller Page 531
static const uint32_t ROMCTL     0x400fe0f0

// Flash Memory Address: see Stellaris LM4F120H5QR Microcontroller Page 497
static const uint32_t FMA        0x400fd000

static const cmd_str1[] = "debug clock ";
static const cmd_str2[] = "debug sreset";
static const cmd_str3[] = "debug creset";
static const cmd_str4[] = "set vectorcatch 0";
static const cmd_str5[] = "debug disable";

int main(int argc, char *argv[])
{
	libusb_context *ctx;
	libusb_device *dev;
	libusb_device_handle *handle;
	int transfered = 0;
	int retval;
	unsigned char data[] = { 0x24, 0x71, 0x52, 0x63, 0x6d, 0x64, 0x2c,
	                         0x36, 0x34, 0x36, 0x35, 0x36, 0x32, 0x37,
	                         0x35, 0x36, 0x37, 0x32, 0x30, 0x36, 0x33,
	                         0x36, 0x63, 0x36, 0x66, 0x36, 0x33, 0x36,
	                         0x62, 0x32, 0x30, 0x30, 0x30, 0x23, 0x66,
	                         0x63 };

	if (libusb_init(&ctx) != 0) {
		fprintf(stderr, "Error initializing libusb\n");
		exit(1);
	}

	// print all error messages
	libusb_set_debug(ctx, 3);

	handle = libusb_open_device_with_vid_pid(ctx, 0x1cbe, 0x00fd);
	if (!handle) {
		printf("Device not found\n");
		exit(1);
	}

#if 0
	dev = libusb_get_device(handle);
#endif
	retval = libusb_claim_interface(handle, 2);
	if (retval != 0) {
		printf("Error claiming interface %d\n", retval);
		exit(1);
	}

	retval = libusb_bulk_transfer(handle, 0x02, data, sizeof(data), &transfered, 0);
	if (retval != 0) {
		printf("Error transmitting data %d\n", retval);
		exit(1);
	}

	libusb_close(handle);
	libusb_exit(ctx);
	return 0;
}
