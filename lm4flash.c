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

static const uint8_t INTERFACE_NR = 0x02;
static const uint8_t ENDPOINT_IN  = 0x83;
static const uint8_t ENDPOINT_OUT = 0x02;

#define START "$"
#define END "#"

#ifdef WIN32
#define snprintf _snprintf
#define SNPRINTF_OFFSET 1
#else
#define SNPRINTF_OFFSET 0
#endif

#define START_LEN strlen(START)
#define END_LEN (strlen(END) + 2)

#define FLASH_BLOCK_SIZE 512

/* Prefix + potentially every flash byte escaped */
#define BUF_SIZE 64 + 2*FLASH_BLOCK_SIZE

static uint8_t flash_block[FLASH_BLOCK_SIZE];
static union {
	char c[BUF_SIZE];
	uint8_t u8[BUF_SIZE];
	uint32_t u32[BUF_SIZE / 4];
} buf;

static uint32_t le32_to_cpu(const uint32_t x)
{
	union {
		uint8_t  b8[4];
		uint32_t b32;
	} _tmp;
	_tmp.b8[3] = x >> 24;
	_tmp.b8[2] = x >> 16;
	_tmp.b8[1] = x >> 8;
	_tmp.b8[0] = x & 0xff;
	return _tmp.b32;
}

#define cpu_to_le32 le32_to_cpu

static int send_command(libusb_device_handle *handle, int size)
{
	int transferred = 0;
	int retval;

#ifdef DEBUG
	int i, col;

	printf("buffer:\n");
	for (i = 0, col = 1; i < size; i++, col++) {
		printf("%02x ", buf.u8[i]);
		if (col == 16) { col = 0; printf("\n"); }
	}
	printf("\n");
#endif

	retval = libusb_bulk_transfer(handle, ENDPOINT_OUT, buf.u8, size, &transferred, 0);
	if (retval != 0 || size != transferred) {
		printf("Error transmitting data %d\n", retval);
	}

	return retval;
}

static int wait_response(libusb_device_handle *handle, int *size)
{
	int retval;
#ifdef DEBUG
	int i;
#endif

	retval = libusb_bulk_transfer(handle, ENDPOINT_IN, buf.u8, BUF_SIZE, size, 0);
	if (retval != 0) {
		printf("Error receiving data %d\n", retval);
	}

#ifdef DEBUG
	printf("wait_response: size=%d\n", *size);
	printf("buffer: ");
	for (i = 0; i < *size; i++)
		printf("0x%02x ", buf.u8[i]);
	printf("\n");
#endif

	return retval;
}

static int checksum_and_send(libusb_device_handle *handle, size_t idx)
{
	size_t i;
	uint8_t sum = 0;
	int retval, transfered;

	if (idx + SNPRINTF_OFFSET + END_LEN > BUF_SIZE)
		return LIBUSB_ERROR_NO_MEM;

	for (i = 1; i < idx; i++)
		sum += buf.u8[i];

	idx += sprintf(buf.c + idx, END "%02x", sum);

	retval = send_command(handle, idx);
	if (retval)
		return retval;

	/* wait for ack (+/-) */
	retval = wait_response(handle, &transfered);
	if (retval)
		return retval;

	/* FIXME: validate transfered here? */

	/* wait for command response */
	retval = wait_response(handle, &transfered);

	/* FIXME: validate transfered here? */

	return retval;
}


static int send_u8_hex(libusb_device_handle *handle, const char *prefix, const char *bytes, size_t num_bytes)
{
	size_t i, idx;

	/* Make sure that everything fits!
	 * START + prefix + hex bytes + END + hex checksum + '\0'
	 */
	if (START_LEN + (prefix ? strlen(prefix) : 0) + (2 * num_bytes) + END_LEN + 1 > BUF_SIZE)
		return LIBUSB_ERROR_NO_MEM;

	idx = sprintf(buf.c, START "%s", prefix);

	for (i = 0; bytes && i < num_bytes; i++)
		idx += sprintf(buf.c + idx, "%02x", bytes[i]);

	return checksum_and_send(handle, idx);
}

static int send_u8_binary(libusb_device_handle *handle, const char *prefix, const char *bytes, size_t num_bytes)
{
	size_t idx;

	/* Make sure that everything fits!
	 * START + prefix + bytes + END + hex checksum + '\0'
	 */
	if (START_LEN + (prefix ? strlen(prefix) : 0) + num_bytes + END_LEN + 1 > BUF_SIZE)
		return LIBUSB_ERROR_NO_MEM;

	idx = sprintf(buf.c, START "%s", prefix);

	memcpy(buf.c + idx, bytes, num_bytes);
	idx += num_bytes;

	return checksum_and_send(handle, idx);
}

static int send_u32(libusb_device_handle *handle, const char *prefix, const uint32_t val, const char *suffix)
{
	size_t idx = snprintf(buf.c, BUF_SIZE, START "%s%08x%s",
			prefix ? prefix : "", val,
			suffix ? suffix : "");

	return checksum_and_send(handle, idx);
}

static int send_u32_u32(libusb_device_handle *handle, const char *prefix, const uint32_t val1, const char *infix, const uint32_t val2, const char *suffix)
{
	size_t idx = snprintf(buf.c, BUF_SIZE, START "%s%08x%s%08x%s",
			prefix ? prefix : "", val1,
			infix ? infix : "", val2,
			suffix ? suffix : "");

	return checksum_and_send(handle, idx);
}


static int send_mem_write(libusb_device_handle *handle, const uint32_t addr, const uint32_t val)
{
	return send_u32_u32(handle, "X", addr, ",4:", val, NULL);
}

static int send_mem_read(libusb_device_handle *handle, const uint32_t addr, uint32_t *val)
{
	int retval = send_u32(handle, "x", addr, ",4");
	if (retval)
		return retval;

	if (val)
		*val = le32_to_cpu(buf.u32[0]);

	return 0;
}

static int send_flash_erase(libusb_device_handle *handle, const uint32_t start, const uint32_t end)
{
	return send_u32_u32(handle, "vFlashErase:", start, ",", end, NULL);
}

static int send_flash_write(libusb_device_handle *handle, const uint32_t addr, const uint8_t *bytes, size_t len)
{
	size_t i;
	char prefix[] = "vFlashWrite:12345678:";
	char by, rawbuf[1024], *buf = rawbuf;

	sprintf(strchr(prefix, ':') + 1, "%08x:", addr);

	for (i = 0; i < len; i++)
		switch (by = bytes[i]) {
		case '#':
		case '$':
		case '}':
			*buf++ = '}';
			by ^= 0x20;
			/* fall through */
		default:
			if (buf >= rawbuf + sizeof(rawbuf))
				return LIBUSB_ERROR_NO_MEM;

			*buf++ = by;
			break;
		}

	i = buf - rawbuf;

	return send_u8_binary(handle, prefix, rawbuf, i) ? -1 : i;
}

#define SEND_COMMAND(cmd) do { \
	int r = send_u8_hex(handle, "qRcmd,", (cmd), sizeof((cmd)) - 1); \
	if (r) \
		return r; \
} while (0)

#define SEND_STRING(str) do { \
	int r = send_u8_hex(handle, (str), NULL, 0); \
	if (r) \
		return r; \
} while (0)

#define MEM_WRITE(address, value) do { \
	int r = send_mem_write(handle, (address), (value)); \
	if (r) \
		return r; \
} while (0)

#define MEM_READ(address, value) do { \
	int r = send_mem_read(handle, (address), (value)); \
	if (r) \
		return r; \
} while (0)

#define FLASH_ERASE(start, end) do { \
	int r = send_flash_erase(handle, (start), (end)); \
	if (r) \
		return r; \
} while (0)

#define FLASH_WRITE(address, data, length) do { \
	int r = send_flash_write(handle, (address), (data), (length)); \
	if (r < (length)) \
		return LIBUSB_ERROR_OTHER; \
} while (0)


/*
 *  This flow is of commands is based on an USB capture of
 *  traffic between LM Flash Programmer and the Stellaris Launchpad
 *  when doing a firmware write
 */
static int write_firmware(libusb_device_handle *handle, FILE *f)
{
	uint32_t val = 0;
	uint32_t addr;
	size_t rdbytes;

	SEND_COMMAND("debug clock \0");
	SEND_STRING("qSupported");
	SEND_STRING("?");
	MEM_WRITE(FPB, 0x3000000);
	MEM_READ(XXX1CTL, &val);
	MEM_READ(XXX1CTL + 4, &val);
	SEND_STRING("?");
	MEM_READ(XXX2CTL, &val);
	SEND_COMMAND("debug sreset");
	MEM_READ(XXX2CTL, &val);
	MEM_READ(ROMCTL, &val);
	MEM_WRITE(ROMCTL, 0x0);
	MEM_READ(XXX2CTL, &val);
	MEM_READ(XXX3CTL, &val);
	MEM_READ(XXX1CTL, &val);
	MEM_READ(XXX1CTL + 4, &val);
	MEM_READ(XXX1CTL + 8, &val);
	MEM_READ(XXX1CTL, &val);
	MEM_READ(XXX4CTL, &val);

	/* XXX: Repeated below, why? */
	MEM_WRITE(FMA, 0x0);
	MEM_READ(XXX2CTL, &val);
	FLASH_ERASE(0, 0);
	SEND_COMMAND("debug creset");
	MEM_READ(XXX2CTL, &val);

	MEM_WRITE(XXX2CTL, 0x0);

	/* XXX: this is the same sequence of the above commands? */
	MEM_WRITE(FMA, 0x200);
	MEM_READ(XXX2CTL, &val);
	FLASH_ERASE(0, 0);
	SEND_COMMAND("debug creset");
	MEM_READ(XXX2CTL, &val);

	MEM_READ(ROMCTL, &val);
	MEM_WRITE(ROMCTL, 0x0);
	MEM_READ(XXX2CTL, &val);

	for (addr = 0; !feof(f); addr += sizeof(flash_block)) {
		rdbytes = fread(flash_block, 1, sizeof(flash_block), f);

		if (rdbytes < sizeof(flash_block) && !feof(f)) {
			perror("fread");
			return LIBUSB_ERROR_OTHER;
		}

		FLASH_WRITE(addr, flash_block, rdbytes);
	}

	SEND_COMMAND("set vectorcatch 0");
	SEND_COMMAND("debug disable");

	/* reset board */
	MEM_WRITE(FPB, 0x3000000);
	SEND_COMMAND("debug hreset");
	SEND_COMMAND("set vectorcatch 0");
	SEND_COMMAND("debug disable");

	return 0;
}

int main(int argc, char *argv[])
{
	libusb_context *ctx = NULL;
	libusb_device_handle *handle = NULL;
	int retval = 1;
	FILE *f = NULL;

	if (argc < 2) {
		printf("usage: %s <binary-file>\n", argv[0]);
		goto done;
	}

	if (libusb_init(&ctx) != 0) {
		fprintf(stderr, "Error initializing libusb\n");
		goto done;
	}

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

	f = fopen(argv[1], "rb");
	if (!f) {
		perror("fopen");
		retval = 1;
		goto done;
	}

	retval = write_firmware(handle, f);

done:
	if (f)
		fclose(f);
	if (handle)
		libusb_close(handle);
	if (ctx)
		libusb_exit(ctx);

	return retval;
}
