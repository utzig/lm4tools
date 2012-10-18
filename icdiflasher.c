/* icdiflasher - TI Stellaris Launchpad compatible ICDI flasher
 * Copyright (C) 2012 Fabio Utzig
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <libusb.h>

//#define DEBUG 1

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
static const char cmd_str6[] = "debug hreset";

static const char str_qRcmd[] = "qRcmd,";
static const char str_qSupported[] = "qSupported";
static const char str_Interrogation[] = "?";
static const char str_X[] = "X";
static const char str_x[] = "x";
static const char str_vFlashErase[] = "vFlashErase";
static const char str_vFlashWrite[] = "vFlashWrite";

static const uint8_t INTERFACE_NR = 0x02;
static const uint8_t ENDPOINT_IN  = 0x83;
static const uint8_t ENDPOINT_OUT = 0x02;

static const char START_CHAR = '$';
static const char END_CHAR   = '#';

static uint8_t buffer[1024];
static uint8_t fdbuffer[512];

static inline char NIBBLE_TO_CHAR(uint8_t nibble)
{
	return nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
}

static int send_command(libusb_device_handle *handle, int size)
{
	int transferred = 0;
	int retval;
	int i, col;

#ifdef DEBUG
	printf("buffer:\n");
	for (i = 0, col = 1; i < size; i++, col++) {
		printf("%02x ", buffer[i]);
		if (col == 16) { col = 0; printf("\n"); }
	}
	printf("\n");
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

static int send_vFlashWrite(libusb_device_handle *handle, uint32_t addr, int fd, int size)
{
	int retval;
	size_t idx;
	uint8_t sum;
	int i;
	int transferred = 0;
	size_t rdsize;

	idx = 0;
	buffer[idx++] = '$';
	memcpy(&buffer[idx], str_vFlashWrite, strlen(str_vFlashWrite));
	idx += strlen(str_vFlashWrite);

	buffer[idx++] = ':';

	for (i = 28; i >= 0; i -= 4)
		buffer[idx++] = NIBBLE_TO_CHAR((addr >> i) & 0x0F);

	buffer[idx++] = ':';

	// FIXME: need to check return and size cant be more than 512
	rdsize = read(fd, fdbuffer, size);

	for (i = 0; i < size; i++) {
		uint8_t by = fdbuffer[i];
		// Escape chars
		if (by == '#' || by == '$' || by == '}') {
			buffer[idx++] = '}';
			buffer[idx++] = by ^ 0x20;
		} else {
			buffer[idx++] = by;
		}
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

	//return retval;
	return rdsize;
}


/*
 *  This flow is of commands is based on an USB capture of
 *  traffic between LM Flash Programmer and the Stellaris Launchpad
 *  when doing a firmware write
 */
static int write_firmware(libusb_device_handle *handle, int fd)
{
	int retval;
	uint32_t val = 0;
	uint32_t addr;

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

	/* XXX: Repeated below, why? */
	retval = send_X(handle, FMA, 4, 0x00000000);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_vFlashErase(handle, 0, 0);
	retval = send_qRcmd(handle, cmd_str3, sizeof(cmd_str3) - 1);
	retval = send_x(handle, XXX2CTL, 4, &val);

	retval = send_X(handle, XXX2CTL, 4, 0x00000000);

	/* XXX: this is the same sequence of the above commands? */
	retval = send_X(handle, FMA, 4, 0x00000200);
	retval = send_x(handle, XXX2CTL, 4, &val);
	retval = send_vFlashErase(handle, 0, 0);
	retval = send_qRcmd(handle, cmd_str3, sizeof(cmd_str3) - 1);
	retval = send_x(handle, XXX2CTL, 4, &val);

	retval = send_x(handle, ROMCTL, 4, &val);
	retval = send_X(handle, ROMCTL, 4, 0x00000000);
	retval = send_x(handle, XXX2CTL, 4, &val);

	for (addr = 0, retval = 512; retval == 512; addr += 0x200)
		retval = send_vFlashWrite(handle, addr, fd, 512);

	retval = send_qRcmd(handle, cmd_str4, sizeof(cmd_str4) - 1);
	retval = send_qRcmd(handle, cmd_str5, sizeof(cmd_str5) - 1);

	// reset board
	retval = send_X(handle, FPB, 4, 0x03000000);
	retval = send_qRcmd(handle, cmd_str6, sizeof(cmd_str6) - 1);
	retval = send_qRcmd(handle, cmd_str4, sizeof(cmd_str4) - 1);
	retval = send_qRcmd(handle, cmd_str5, sizeof(cmd_str5) - 1);
}

int main(int argc, char *argv[])
{
	libusb_context *ctx = NULL;
	libusb_device *dev = NULL;
	libusb_device_handle *handle = NULL;
	int retval = 1;
	int fd = -1;

	if (argc < 2) {
		printf("usage: %s <binary-file>\n", argv[0]);
		goto done;
	}

	if (libusb_init(&ctx) != 0) {
		fprintf(stderr, "Error initializing libusb\n");
		goto done;
	}

	// print all error messages
	libusb_set_debug(ctx, 0);

	/* FIXME: should not be using this function call! */
	handle = libusb_open_device_with_vid_pid(ctx, 0x1cbe, 0x00fd);
	if (!handle) {
		printf("Device not found\n");
		goto done;
	}

	retval = libusb_claim_interface(handle, INTERFACE_NR);
	if (retval != 0) {
		printf("Error claiming interface %d\n", retval);
		goto done;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("open");
		retval = 1;
		goto done;
	}

	retval = write_firmware(handle, fd);

done:
	if (fd != -1)
		close(fd);
	if (handle)
		libusb_close(handle);
	if (ctx)
		libusb_exit(ctx);

	return retval;
}
