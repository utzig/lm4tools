/* lm4flash - TI Stellaris Launchpad ICDI flasher
 * Copyright (C) 2012-2018 Fabio Utzig <utzig@utzig.org>
 * Copyright (C) 2012 Peter Stuge <peter@stuge.se>
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
#include <ctype.h>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <libusb.h>

//#define DEBUG 1

#define ICDI_VID 0x1cbe
#define ICDI_PID 0x00fd

// FlashPatch Control Register: see ARM Av7mRM C1.11.3
static const uint32_t FP_CTRL  = 0xe0002000;

// Debug Halting Control and Status Register: see ARM Av7mRM C1.6.2
static const uint32_t DHCSR    = 0xe000edf0;

// Device Identification: see Stellaris LM4F120H5QR Microcontroller Section 5.5
static const uint32_t DID0     = 0x400fe000;
static const uint32_t DID1     = 0x400fe004;

// Device Identification: see Stellaris LM4F120H5QR Microcontroller Section 5.5
static const uint32_t DC0      = 0x400fe008;

// Run-Mode Clock Configuration: Stellaris LM4F120H5QR Microcontroller Section 5.5
static const uint32_t RCC      = 0x400fe060;

// Non-Volatile Memory Information: Stellaris LM4F120H5QR Microcontroller Section 5.6
static const uint32_t NVMSTAT  = 0x400fe1a0;

// Rom Control: see Stellaris LM4F120H5QR Microcontroller Page 531
static const uint32_t ROMCTL   = 0x400fe0f0;

// Flash Memory Address: see Stellaris LM4F120H5QR Microcontroller Page 497
static const uint32_t FMA      = 0x400fd000;

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
#define FLASH_ERASE_SIZE 1024

/* Prefix + potentially every flash byte escaped */
#define BUF_SIZE 64 + 2*FLASH_BLOCK_SIZE

static uint8_t flash_block[FLASH_BLOCK_SIZE];
static union {
	char c[BUF_SIZE];
	uint8_t u8[BUF_SIZE];
	uint32_t u32[BUF_SIZE / 4];
} buf;

void show_version(void)
{
	printf("%s",
	       "LM4Flash version 0.1.3 - Flasher for Stellaris Launchpad ICDI boards\n"
	       "Copyright (C) 2012-2018 Fabio Utzig <utzig@utzig.org>\n"
	       "Copyright (C) 2012 Peter Stuge <peter@stuge.se>\n"
	       "This is free software; see the source for copying conditions.  There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR "
	       "PURPOSE.\n"
	);
}

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

static int do_verify = 0;
static int erase_used = 0;
static uint32_t start_addr = 0;

#define cpu_to_le32 le32_to_cpu

#ifdef DEBUG
static void pretty_print_buf(uint8_t *b, int size)
{
#define PP_LINESIZE    80
#define PP_NUM_P_LINE  16
#define PP_HEX_COL     7
#define PP_ASC_COL     56

	int i, pos;
	char linebuf[PP_LINESIZE];

	memset(linebuf, ' ', sizeof linebuf);
	linebuf[PP_ASC_COL + PP_NUM_P_LINE] = 0;
	for (i = 0; i < size; i++) {
		if (((i % PP_NUM_P_LINE) == 0)) {
			if (i) {
				printf("%s\n", linebuf);
				memset(linebuf, ' ', sizeof linebuf);
				linebuf[PP_ASC_COL + PP_NUM_P_LINE] = 0;
			}
			sprintf(linebuf, "%04x : ", i);
			linebuf[PP_ASC_COL] = ' ';
			linebuf[PP_HEX_COL] = ' ';
		}
		pos = PP_HEX_COL + ((i % PP_NUM_P_LINE) * 3);
		sprintf(linebuf + pos, "%02x", b[i]);
		linebuf[pos + 2] = ' ';
		linebuf[(i % PP_NUM_P_LINE) + PP_ASC_COL] = isprint(b[i]) ? b[i] : '.';
	}
	printf("%s\n", linebuf);
}
#endif

static int send_command(libusb_device_handle *handle, int size)
{
	int transferred = 0;
	int retval;

#ifdef DEBUG
	printf(">>> sending %d bytes\n", size);
	pretty_print_buf(buf.u8, size);
#endif

	retval = libusb_bulk_transfer(handle, ENDPOINT_OUT, buf.u8, size, &transferred, 0);
	if (retval != 0 || size != transferred) {
		printf("Error transmitting data %d\n", retval);
	}

	return retval;
}

static int wait_response(libusb_device_handle *handle, int *has_ack, int *size)
{
	int retval;
	int transferred = 0;

	*has_ack = 0;
	*size = 0;

	do {
		retval = libusb_bulk_transfer(handle,
		                              ENDPOINT_IN,
		                              &buf.u8[*size],
		                              BUF_SIZE - *size,
		                              &transferred,
		                              0);
		if (retval != 0) {
			printf("Error receiving data %d\n", retval);
			return retval;
		}

		if (transferred >= 1 && buf.c[0] == '+')
			*has_ack = 1;

		*size += transferred;

	} while ((*size < 3) || (buf.c[*size - 3] != '#'));

#ifdef DEBUG
	printf("<<< received %d bytes\n", *size);
	pretty_print_buf(buf.u8, *size);
#endif

	return retval;
}

static int checksum_and_send(libusb_device_handle *handle, size_t idx, int *xfer)
{
	size_t i;
	uint8_t sum = 0;
	int retval, transferred;
	int has_ack;

	if (idx + SNPRINTF_OFFSET + END_LEN > BUF_SIZE)
		return LIBUSB_ERROR_NO_MEM;

	for (i = 1; i < idx; i++)
		sum += buf.u8[i];

	idx += sprintf(buf.c + idx, END "%02x", sum);

	retval = send_command(handle, idx);
	if (retval)
		return retval;

	retval = wait_response(handle, &has_ack, &transferred);
	if (retval)
		return retval;

	if (!has_ack)
		return LIBUSB_ERROR_OTHER;

	if (xfer)
		*xfer = transferred;

	/* FIXME: validate transferred here? */

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

	return checksum_and_send(handle, idx, NULL);
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

	return checksum_and_send(handle, idx, NULL);
}

static int send_u32(libusb_device_handle *handle, const char *prefix, const uint32_t val, const char *suffix)
{
	size_t idx = snprintf(buf.c, BUF_SIZE, START "%s%08x%s",
			prefix ? prefix : "", val,
			suffix ? suffix : "");

	return checksum_and_send(handle, idx, NULL);
}

static int send_u32_u32(libusb_device_handle *handle, const char *prefix, const uint32_t val1, const char *infix, const uint32_t val2, const char *suffix)
{
	size_t idx = snprintf(buf.c, BUF_SIZE, START "%s%08x%s%08x%s",
			prefix ? prefix : "", val1,
			infix ? infix : "", val2,
			suffix ? suffix : "");

	return checksum_and_send(handle, idx, NULL);
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
	if (val) {
		uint32_t u = 0;
		u |= buf.u8[8] << 24;
		u |= buf.u8[7] << 16;
		u |= buf.u8[6] << 8;
		u |= buf.u8[5];
		*val = le32_to_cpu(u);
	}

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

static int decode_buffer(char *inbuf, int insize, char *outbuf, int outsize)
{
	int i;
	char by, *bp = outbuf;

	for (i = 0; i < insize; i++) {
		switch (by = inbuf[i]) {
			case '}':
				by = inbuf[++i] ^ 0x20;
				/* fall through */
			default:
				if (bp >= outbuf + outsize)
					return LIBUSB_ERROR_NO_MEM;
				*bp++ = by;
				break;
		}
	}

	return 0;
}

static int send_flash_verify(libusb_device_handle *handle, const uint32_t addr, const uint8_t *bytes, size_t len)
{
	size_t i, j;
	char rawbuf[1024];
	int retval, transferred;

	size_t idx = snprintf(buf.c, BUF_SIZE, START "x%x,%x", addr, (uint32_t)len);

	retval = checksum_and_send(handle, idx, &transferred);
	if (retval)
		return retval;

	decode_buffer(buf.c, transferred, rawbuf, sizeof rawbuf);

	if (strncmp(rawbuf, "+$OK:", 5) != 0)
		return LIBUSB_ERROR_OTHER;

	for (i = 0, j = strlen("+$OK:"); i < len; i++, j++) {
		if (bytes[i] != (uint8_t)rawbuf[j]) {
			return LIBUSB_ERROR_OTHER;
		}
	}

	return 0;
}

static int print_icdi_version(libusb_device_handle *handle)
{
	int retval = 0;
	char rawbuf[32];
	size_t i, idx;
	int transferred = 0;
	const char *cmd = "version";

	idx = sprintf(buf.c, START "qRcmd,");

	for (i = 0; i < strlen(cmd); i++)
		idx += sprintf(buf.c + idx, "%02x", cmd[i]);

	retval = checksum_and_send(handle, idx, &transferred);
	if (retval)
		return retval;

	decode_buffer(buf.c, transferred, rawbuf, sizeof rawbuf);

	if (strncmp(rawbuf, "+$", 2) != 0)
		return LIBUSB_ERROR_OTHER;

	printf("ICDI version: ");
	for (i = strlen("+$"); rawbuf[i] != '#'; i += 2) {
		char r = rawbuf[i];
		char c = (r <= '9') ? (r - '0') << 4 : (r - 'a' + 10) << 4;
		r = rawbuf[i+1];
		c |= (r <= '9') ? r - '0' : r - 'a' + 10;
		printf("%c", c);
	}

	return 0;
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
	int retval = 0;
	uint32_t size;

	print_icdi_version(handle);

	SEND_COMMAND("debug clock \0");
	SEND_STRING("qSupported");
	SEND_STRING("?");
	MEM_WRITE(FP_CTRL, 0x3000000);
	MEM_READ(DID0, &val);
	MEM_READ(DID1, &val);
	SEND_STRING("?");
	MEM_READ(DHCSR, &val);
	SEND_COMMAND("debug sreset");
	MEM_READ(DHCSR, &val);
	MEM_READ(ROMCTL, &val);
	MEM_WRITE(ROMCTL, 0x0);
	MEM_READ(DHCSR, &val);
	MEM_READ(RCC, &val);
	MEM_READ(DID0, &val);
	MEM_READ(DID1, &val);
	MEM_READ(DC0, &val);
	MEM_READ(DID0, &val);
	MEM_READ(NVMSTAT, &val);

	MEM_WRITE(FMA, 0x0);
	MEM_READ(DHCSR, &val);

	if (erase_used) {
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		for (addr = start_addr; addr < (start_addr + size); addr += FLASH_ERASE_SIZE)
			FLASH_ERASE(addr, FLASH_ERASE_SIZE);
		fseek(f, 0, SEEK_SET);
	} else {
		FLASH_ERASE(0, 0);
	}

	SEND_COMMAND("debug creset");
	MEM_READ(DHCSR, &val);

	MEM_WRITE(DHCSR, 0x0);

	MEM_READ(ROMCTL, &val);
	MEM_WRITE(ROMCTL, 0x0);
	MEM_READ(DHCSR, &val);

	for (addr = start_addr; !feof(f); addr += sizeof(flash_block)) {
		rdbytes = fread(flash_block, 1, sizeof(flash_block), f);

		if (rdbytes < sizeof(flash_block) && !feof(f)) {
			perror("fread");
			return LIBUSB_ERROR_OTHER;
		}

		/*
		 * Avoid writing a buffer with zero-sized content which can
		 * happen when the input file has a size multiple of flash_block
		 */
		if (rdbytes)
			FLASH_WRITE(addr, flash_block, rdbytes);
	}

	if (do_verify) {
		fseek(f, 0, SEEK_SET);

		for (addr = start_addr; !feof(f); addr += sizeof(flash_block)) {
			rdbytes = fread(flash_block, 1, sizeof(flash_block), f);

			if (rdbytes < sizeof(flash_block) && !feof(f)) {
				perror("fread");
				return LIBUSB_ERROR_OTHER;
			}

			/* On error don't return immediately... finish resetting the board */
			retval = send_flash_verify(handle, addr, flash_block, rdbytes);
			if (retval) {
				printf("Error verifying flash\n");
				break;
			}
		}
	}

	SEND_COMMAND("set vectorcatch 0");
	SEND_COMMAND("debug disable");

	/* reset board */
	MEM_WRITE(FP_CTRL, 0x3000000);
	SEND_COMMAND("debug hreset");
	SEND_COMMAND("set vectorcatch 0");
	SEND_COMMAND("debug disable");

	return retval;
}


enum flasher_error {
	FLASHER_SUCCESS,
	FLASHER_ERR_LIBUSB_FAILURE,
	FLASHER_ERR_NO_DEVICES,
	FLASHER_ERR_MULTIPLE_DEVICES,
};


static enum flasher_error
flasher_find_matching_device(
	libusb_context *ctx,
	libusb_device **matching_device_out,
	enum libusb_error *libusb_error_out,
	int vendor_id,
	int product_id,
	const char *serial)
{
	struct libusb_device_descriptor device_descriptor;
#ifndef __APPLE__
	char descriptor_buffer[256];
#endif
	libusb_device **device_list = NULL;
	libusb_device *matching_device = NULL;
#ifndef __APPLE__
	libusb_device_handle *handle;
#endif
	enum flasher_error flasher_error;
	enum libusb_error libusb_error;

	int retval;
	int device_count;
	int device_index;

	/* Enumerate all USB devices */
	retval = libusb_get_device_list(ctx, &device_list);
	if (retval < 0) {
		libusb_error = retval;
		flasher_error = FLASHER_ERR_LIBUSB_FAILURE;
		fprintf(stderr, "Unable to get enumerate USB devices: %s\n",
		        libusb_error_name(libusb_error));
		goto out;
	} else {
		device_count = retval;
		flasher_error = FLASHER_SUCCESS;
		libusb_error = LIBUSB_SUCCESS;
	}

	/* Assume no devices were found */
	flasher_error = FLASHER_ERR_NO_DEVICES;

	/* Walk the list of devices and try to match some */
	for (device_index = 0; device_index < device_count; ++device_index) {
		retval = libusb_get_device_descriptor(
		           device_list[device_index], &device_descriptor);
		if (retval < 0) {
			fprintf(stderr, "Unable to get device descritor: %s\n",
			        libusb_error_name(retval));
			libusb_error = retval;
			flasher_error = FLASHER_ERR_LIBUSB_FAILURE;
			goto out;
		}
		/* Skip devices that have incorrect vendor and product IDs */
		if (device_descriptor.idVendor != vendor_id ||
		    device_descriptor.idProduct != product_id) {
			continue;
		}

#ifndef __APPLE__
		/* Open each device so that we can read the serial number */
		retval = libusb_open(device_list[device_index], &handle);
		if (retval < 0) {
			fprintf(stderr, "Unable to open USB device: %s\n",
			        libusb_error_name(retval));
			continue;
		}
		/* Read the serial number */
		retval = libusb_get_string_descriptor_ascii(
			handle, device_descriptor.iSerialNumber,
			(unsigned char *)descriptor_buffer, sizeof descriptor_buffer);
		/* Close the handle as we won't need it below */
		libusb_close(handle);
		if (retval < 0) {
			fprintf(stderr, "Unable to get device serial number: %s\n",
			        libusb_error_name(retval));
			continue;
		}
		printf("Found ICDI device with serial: %s\n", descriptor_buffer);
		/* Skip devices with serial that does not match */
		if (serial != NULL && strcmp(serial, descriptor_buffer) != 0)
			continue;
#endif
		if (matching_device == NULL) {
			flasher_error = FLASHER_SUCCESS;
			matching_device = device_list[device_index];
		} else {
			/* If there's a device found already then abort */
			flasher_error = FLASHER_ERR_MULTIPLE_DEVICES;
			/* Don't try returning arbitrary "first" device */
			matching_device = NULL;
			goto out;
		}
	}

out:
	/* Ref the matching device as we'll be returning it */
	if (matching_device != NULL && matching_device_out != NULL) {
		libusb_ref_device(matching_device);
		*matching_device_out = matching_device;
	}
	/* Release the device list */
	if (device_list != NULL)
		libusb_free_device_list(device_list, 1);
	/* Store libusb error if requested */
	if (libusb_error_out != NULL)
		*libusb_error_out = libusb_error;
	/* Return the flasher error code */
	return flasher_error;
}


static void flasher_usage()
{
	printf("Usage: lm4flash [options] <binary-file>\n");
	printf("\t-V\n");
	printf("\t\tPrint version information\n");
	printf("\t-h\n");
	printf("\t\tPrint usage information\n");
	printf("\t-v\n");
	printf("\t\tEnables verification after write\n");
	printf("\t-E\n");
	printf("\t\tOnly erase blocks where binary file will be written\n");
	printf("\t-S address\n");
	printf("\t\tWrite binary at the given address (in hexadecimal)\n");
	printf("\t-s SERIAL\n");
	printf("\t\tFlash device with the following serial\n");
}


static int flasher_flash(const char *serial, const char *rom_name)
{
	libusb_context *ctx = NULL;
	libusb_device *device = NULL;
	libusb_device_handle *handle = NULL;
	int retval;
	FILE *f = NULL;

	retval = libusb_init(&ctx);

	if (retval != 0) {
		fprintf(stderr, "Error initializing libusb: %s\n",
		        libusb_error_name(retval));
		goto done;
	}

	switch (retval = flasher_find_matching_device(
	        ctx, &device, &retval, ICDI_VID, ICDI_PID, serial)) {
	case FLASHER_SUCCESS:
		break;
	case FLASHER_ERR_LIBUSB_FAILURE:
		fprintf(stderr, "Error while matching ICDI devices: %s\n",
		        libusb_error_name(retval));
		goto done;
	case FLASHER_ERR_NO_DEVICES:
		fprintf(stderr, "Unable to find any ICDI devices\n");
		goto done;
	case FLASHER_ERR_MULTIPLE_DEVICES:
		if (serial == NULL)
			fprintf(stderr, "Found multiple ICDI devices\n");
		else
			fprintf(stderr, "Found ICDI serial number collision!\n");
		goto done;
	}

	retval = libusb_open(device, &handle);
	if (retval != 0) {
		fprintf(stderr, "Error opening selected device: %s\n",
		        libusb_error_name(retval));
		goto done;
	}

	retval = libusb_claim_interface(handle, INTERFACE_NR);
	if (retval != 0) {
		fprintf(stderr, "Error claiming interface: %s\n",
		        libusb_error_name(retval));
		goto done;
	}

	f = fopen(rom_name, "rb");
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
	if (device)
		libusb_unref_device(device);
	if (ctx)
		libusb_exit(ctx);

	return retval;
}


int main(int argc, char *argv[])
{
	const char *serial = NULL;
	const char *rom_name = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "VES:hvs:")) != -1) {
		switch (opt) {
		case 'V':
			show_version();
			return 0;
		case 'h':
			flasher_usage();
			return 0;
		case 'E':
			erase_used = 1;
			break;
		case 'S':
			start_addr = strtol(optarg, NULL, 16);
			/* force erasing only the used blocks */
			erase_used = 1;
			break;
		case 'v':
			do_verify = 1;
			break;
		case 's':
			serial = optarg;
			break;
		default:
			flasher_usage();
			return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		flasher_usage();
		return EXIT_FAILURE;
	} else
		rom_name = argv[optind];

	if (start_addr && (start_addr % FLASH_ERASE_SIZE)) {
		printf("Address given to -S must be 0x%x aligned\n", FLASH_ERASE_SIZE);
		return EXIT_FAILURE;
	}

	return flasher_flash(serial, rom_name);
}
