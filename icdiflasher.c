#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#define DEBUG 1

// Flash Patch and Breakpoint: see ARM Debug Interface V5 Architecture Specification
static const uint32_t FPB     = 0xe0002000;

// Rom Control: see Stellaris LM4F120H5QR Microcontroller Page XXX
static const uint32_t XXX1CTL  = 0x400fe000;
static const uint32_t XXX2CTL  = 0xe000edf0;
static const uint32_t XXX3CTL  = 0x400fe060;
static const uint32_t XXX4CTL  = 0x400fe1a0;

// Rom Control: see Stellaris LM4F120H5QR Microcontroller Page 531
static const uint32_t ROMCTL  = 0x400fe0f0;

// Flash Memory Address: see Stellaris LM4F120H5QR Microcontroller Page 497
static const uint32_t FMA     = 0x400fd000;

static const char cmd_str1[] = "debug clock \0";
static const char cmd_str2[] = "debug sreset";
static const char cmd_str3[] = "debug creset";
static const char cmd_str4[] = "set vectorcatch 0";
static const char cmd_str5[] = "debug disable";

static const char str_qRcmd[] = "qRcmd,";
static const char str_qSupported[] = "qSupported";
static const char str_Interrogation[] = "?";
static const char str_X[] = "X";
static const char str_x[] = "x";
static const char str_vFlashErase[] = "vFlashErase";

static const uint8_t INTERFACE_NR = 0x02;
static const uint8_t ENDPOINT_IN  = 0x83;
static const uint8_t ENDPOINT_OUT = 0x02;

static const char START_CHAR = '$';
static const char END_CHAR   = '#';

static uint8_t buffer[512];

static inline char NIBBLE_TO_CHAR(uint8_t nibble)
{
	return nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
}

static int send_command(libusb_device_handle *handle, int size)
{
	int transferred = 0;
	int retval;
	int i;

#ifdef DEBUG
	printf("buffer: [");
	for (i = 0; i < size; i++)
		printf("%02x ", buffer[i]);
	printf("]\n");
#endif

	retval = libusb_bulk_transfer(handle, ENDPOINT_OUT, buffer, size, &transferred, 0);
	if (retval != 0 || size != transferred) {
		printf("Error transmitting data %d\n", retval);
	}

	return retval;
}

static int wait_response(libusb_device_handle *handle, int *size)
{
	int retval;
	int i;

	retval = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, sizeof(buffer), size, 0);
	if (retval != 0) {
		printf("Error receiving data %d\n", retval);
	}

#ifdef DEBUG
	printf("wait_response: size=%d\n", *size);
	printf("buffer: ");
	for (i = 0; i < *size; i++)
		printf("0x%02x ", buffer[i]);
	printf("\n");
#endif

	return retval;
}

static int send_qRcmd(libusb_device_handle *handle, const char *cmd, size_t size)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = '$';
	memcpy(&buffer[idx], str_qRcmd, strlen(str_qRcmd));
	idx += strlen(str_qRcmd);

	for (i = 0; i < size; i++) {
		buffer[idx++] = NIBBLE_TO_CHAR(cmd[i] >> 4);
		buffer[idx++] = NIBBLE_TO_CHAR(cmd[i] & 0x0F);
	}

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int send_qSupported(libusb_device_handle *handle)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = '$';
	memcpy(&buffer[idx], str_qSupported, strlen(str_qSupported));
	idx += strlen(str_qSupported);

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int send_Interrogation(libusb_device_handle *handle)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = '$';
	memcpy(&buffer[idx], str_Interrogation, strlen(str_Interrogation));
	idx += strlen(str_Interrogation);

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int send_X(libusb_device_handle *handle, uint32_t addr, int size, uint32_t val)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = START_CHAR;
	memcpy(&buffer[idx], str_X, strlen(str_X));
	idx += strlen(str_X);

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((addr >> i) & 0x0F);

	// FIXME: should use size but here it's always uint32_t
	buffer[idx++] = ',';
	buffer[idx++] = '4';
	buffer[idx++] = ':';

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((val >> i) & 0x0F);

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int send_x(libusb_device_handle *handle, uint32_t addr, int size, uint32_t *val)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = START_CHAR;
	memcpy(&buffer[idx], str_x, strlen(str_x));
	idx += strlen(str_x);

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((addr >> i) & 0x0F);

	// FIXME: should use size but here it's always uint32_t
	buffer[idx++] = ',';
	buffer[idx++] = '4';

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int send_vFlashErase(libusb_device_handle *handle, uint32_t start, uint32_t end)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;

	idx = 0;
	buffer[idx++] = START_CHAR;
	memcpy(&buffer[idx], str_vFlashErase, strlen(str_vFlashErase));
	idx += strlen(str_vFlashErase);

	buffer[idx++] = ':';

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((start >> i) & 0x0F);

	// FIXME: should use size but here it's always uint32_t
	buffer[idx++] = ',';

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((end >> i) & 0x0F);

	buffer[idx] = '#';

	for (i = 1, sum = 0; i < idx; i++)
		sum += buffer[i];

	buffer[++idx] = NIBBLE_TO_CHAR(sum >> 4);
	buffer[++idx] = NIBBLE_TO_CHAR(sum & 0x0F);
	idx++;

#ifdef DEBUG
	printf("%s\n", __func__);
#endif

	retval = send_command(handle, idx);
	// wait for ack (+/-)
	retval = wait_response(handle, &transferred);
	// wait for command response
	retval = wait_response(handle, &transferred);

	return retval;
}

static int init(libusb_device_handle *handle)
{
	int retval;
	uint32_t val = 0;

	retval = send_qRcmd(handle, cmd_str1, sizeof(cmd_str1) - 1);
	retval = send_qSupported(handle);
	retval = send_Interrogation(handle);
	retval = send_X(handle, FPB, 4, 0x03000000);
	retval = send_x(handle, XXX1CTL, 4, &val);
	retval = send_x(handle, XXX1CTL + 4, 4, &val);
	retval = send_Interrogation(handle);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_qRcmd(handle, cmd_str2, sizeof(cmd_str2) - 1);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_x(handle, ROMCTL, 4, &val);
	retval = send_X(handle, ROMCTL, 4, 0x00000000);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_x(handle, XXX3CTL, 4, &val);
	retval = send_x(handle, XXX1CTL, 4, &val);
	retval = send_x(handle, XXX1CTL + 4, 4, &val);
	retval = send_x(handle, XXX1CTL + 8, 4, &val);
	retval = send_x(handle, XXX1CTL, 4, &val);
	retval = send_x(handle, XXX4CTL, 4, &val);
	retval = send_X(handle, FMA, 4, 0x00000000);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_vFlashErase(handle, 0, 0);
	retval = send_qRcmd(handle, cmd_str3, sizeof(cmd_str3) - 1);
}

int main(int argc, char *argv[])
{
	libusb_context *ctx;
	libusb_device *dev;
	libusb_device_handle *handle;
	int retval;

	if (libusb_init(&ctx) != 0) {
		fprintf(stderr, "Error initializing libusb\n");
		exit(1);
	}

	// print all error messages
	libusb_set_debug(ctx, 0);

	/* FIXME: should not be using this function call! */
	handle = libusb_open_device_with_vid_pid(ctx, 0x1cbe, 0x00fd);
	if (!handle) {
		libusb_exit(ctx);
		printf("Device not found\n");
		exit(1);
	}

	retval = libusb_claim_interface(handle, INTERFACE_NR);
	if (retval != 0) {
		printf("Error claiming interface %d\n", retval);
		exit(1);
	}

	retval = init(handle);

	libusb_close(handle);
	libusb_exit(ctx);
	return 0;
}
