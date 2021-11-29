// SPDX-License-Identifier: GPL-2.0-only
/*
 * asus_qca_fix_checksum.c : checksum fix for ASUS QCA/QCN SoC uImage
 *
 * Copyright (C) 2021 Alisha Kim <daebo01@playmp3.kr>
 *
 * Based on:
 * 		uimage_padhdr.c : add zero paddings after the tail of uimage header
 * 		Copyright (C) 2019 NOGUCHI Hiroshi <drvlabo@gmail.com>
 * 
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <zlib.h>

/* from asuswrt opensource */
#define MAX_STRING 12
#define MAX_VER 5

typedef struct
{
	uint8_t major;
	uint8_t minor;
} version_t;

/* 
 * ASUS QCA/QCN Custom Header
 */
typedef struct
{
	version_t kernel;
	version_t fs;
	char productid[MAX_STRING];
	uint16_t sn;
	uint16_t en;
	uint8_t pkey;
	uint8_t key;
	version_t hw[MAX_VER];
} TAIL;

/* from u-boot/include/image.h */
#define IH_MAGIC 0x27051956 /* Image Magic Number		*/
#define IH_NMLEN 32			/* Image Name Length		*/

/*
 * Legacy format image header,
 * all data in network byte order (aka natural aka bigendian).
 */
typedef struct image_header
{
	uint32_t ih_magic; /* Image Header Magic Number	*/
	uint32_t ih_hcrc;  /* Image Header CRC Checksum	*/
	uint32_t ih_time;  /* Image Creation Timestamp	*/
	uint32_t ih_size;  /* Image Data Size			*/
	uint32_t ih_load;  /* Data	 Load  Address		*/
	uint32_t ih_ep;	   /* Entry Point Address		*/
	uint32_t ih_dcrc;  /* Image Data CRC Checksum	*/
	uint8_t ih_os;	   /* Operating System			*/
	uint8_t ih_arch;   /* CPU architecture			*/
	uint8_t ih_type;   /* Image Type				*/
	uint8_t ih_comp;   /* Compression Type			*/
	union
	{
		uint8_t ih_name[IH_NMLEN]; /* Image Name			*/
		TAIL tail;				   /* Asuswrt Custom Tail	*/
	} u;
} image_header_t;

void fix_checksum(uint8_t *image, off_t image_len, TAIL *tail)
{
	image_header_t *header = (image_header_t *)image;

	uint32_t checksum_a_offset = 0; // image first byte
	uint32_t checksum_b_offset = (ntohl(header->ih_size) + sizeof(image_header_t)) >> 1;

	uint8_t checksum_a = image[checksum_a_offset];
	uint8_t checksum_b;

	uint32_t recalc_crc;

	if (image_len < checksum_b_offset)
	{
		fprintf(stderr, "too small uImage size\n");
		exit(1);
	}

	checksum_b = image[checksum_b_offset];

	tail->key = checksum_a + ~checksum_b;

	// copy an existing image name
	memcpy(&tail->productid, &header->u.ih_name, sizeof(tail->productid) - 1);

	// overwrite asus custom header to image name field
	header->u.tail = *tail;

	header->ih_hcrc = 0;
	recalc_crc = crc32(0, image, sizeof(image_header_t));
	header->ih_hcrc = htonl(recalc_crc);
}

void usage(char *prog)
{
	fprintf(stderr, "%s -i <input_uimage_file> -o <output_file>\n", prog);
	fprintf(stderr, "		-v <asuswrt version (ex. 3.0.0.4.382.52482)>");
}

int main(int argc, char *argv[])
{
	struct stat statbuf;
	uint8_t *filebuf;
	int ifd;
	int ofd;
	ssize_t rsz;
	int opt;
	char *infname = NULL;
	char *outfname = NULL;
	char *version = NULL;
	TAIL tail = {};

	while ((opt = getopt(argc, argv, "i:o:v:")) != -1)
	{
		switch (opt)
		{
		case 'i':
			infname = optarg;
			break;
		case 'o':
			outfname = optarg;
			break;
		case 'v':
			version = optarg;
			if (6 != sscanf(
						 version, "%hhu.%hhu.%hhu.%hhu.%hu.%hu",
						 &tail.kernel.major, &tail.kernel.minor,
						 &tail.fs.major, &tail.fs.minor,
						 &tail.sn, &tail.en))
				fprintf(stderr, "Version %s doesn't match suppored 6-digits format\n", version);
			break;
		default:
			break;
		}
	}

	if (!infname || !outfname || !version)
	{
		usage(argv[0]);
		exit(1);
	}

	ifd = open(infname, O_RDONLY);
	if (ifd < 0)
	{
		fprintf(stderr,
				"could not open input file. (errno = %d)\n", errno);
		exit(1);
	}

	ofd = open(outfname, O_WRONLY | O_CREAT, 0644);
	if (ofd < 0)
	{
		fprintf(stderr,
				"could not open output file. (errno = %d)\n", errno);
		exit(1);
	}

	if (fstat(ifd, &statbuf) < 0)
	{
		fprintf(stderr,
				"could not fstat input file. (errno = %d)\n", errno);
		exit(1);
	}

	filebuf = malloc(statbuf.st_size);
	if (!filebuf)
	{
		fprintf(stderr, "buffer allocation failed\n");
		exit(1);
	}

	rsz = read(ifd, filebuf, statbuf.st_size);
	if (rsz != statbuf.st_size)
	{
		fprintf(stderr,
				"could not read input file (errno = %d).\n", errno);
		exit(1);
	}

	fix_checksum(filebuf, statbuf.st_size, &tail);

	rsz = write(ofd, filebuf, statbuf.st_size);
	if (rsz != statbuf.st_size)
	{
		fprintf(stderr,
				"could not write output file (errnor = %d).\n", errno);
		exit(1);
	}

	return 0;
}
