/*
----------------------------------------------------------------------------
"THE BEER-WARE LICENSE" (Revision 42):
<gvb@santarago.org> wrote this file. As long as you retain this notice you
can do whatever you want with this stuff. If we meet some day, and you think
this stuff is worth it, you can buy me a beer in return.
----------------------------------------------------------------------------
*/

/*
 * pnghash.c
 *
 * This small utility takes one or two PNG files as arguments and calculates
 * perceptual hash values over it. If two PNG files are supplied the hamming
 * distance between the hashes are also calculated. Perceptual hashes are very
 * useful when one wants to find similar images based on their content. Please
 * note that this tool only supports PNG files in RGB+A format and it does only
 * rudimentary PNG parsing. This means it only supports the required chunks
 * and features such as ICC color profiles or the sRGP color space as not 
 * used. This does not seem to have much of an effect on the actual usefulness
 * of the tool though.
 *
 * The perceptual hash algorithms implemented are:
 *
 * - dhash: this algorithm converts the image to grayscale and then resizes
 *          it to an image of 9x8 pixels. The hash is then calculated by,
 *          from left to right, top to bottom, setting each bit of the 64-bit
 *          hash value whether the grayness of each pixel is less than the
 *          pixel following it.
 *
 * - ahash: this algorithm converts the image to grayscale and then resizes
 *          it to an image of 8x8 pixels. The hash is then calculated by,
 *          from left to right, top to bottom, setting each bit of the 64-bit
 *          hash value whether the grayness of each pixel is less or more than
 *          the calculated mean color of all the pixels in the image.
 *
 * These algorithms were implemented based upon a description as provided Dr.
 * Neal Krawetz on his Hacker Factor blog. See the following links for more
 * information:
 *
 * - http://www.hackerfactor.com/blog/?/archives/529-Kind-of-Like-That.html
 * - http://www.hackerfactor.com/blog/?/archives/432-Looks-Like-It.html
 *
 *  Compile with: gcc -Wall -Werror -lz -lm pnghash.c -o pnghash
 *  Run: ./pnghash <file1.png> [file2.png]
 *
 *  Output will be the 64-bit hexadecimal values for dhash and ahash
 *  respectively. If two PNG files are specified the third line will
 *  contain the calculated hamming distances between the two dhashes and
 *  ahashes respecitvely.
 *
 * Version history:
 * - 0.1 - September 15th, 2015 - initial version
 *
 * By Vincent Berg <gvb@santarago.org>
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <math.h>
#include <zlib.h>

struct png {
	uint32_t w;
	uint32_t h;
	uint8_t ct;
	uint8_t * px;
};

void
pfatal(const char * msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

void
fatal(const char * msg)
{
	fprintf(stderr, "%s\n", msg);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

static void
xread(int fd, char * buf, size_t sz)
{
	char * p;
	ssize_t ret;

	p = buf;
	do {
		ret = read(fd, p, sz);
		if (ret > 0) {
			sz -= ret;
			p += ret;
			if (!sz) break;
		}
	} while (ret == -1 && errno != EAGAIN && errno != EINTR);
	if (ret == -1) {
		fatal("error while reading");
	}
}

static int
png_parse_file(const char * fn, struct png * res)
{
	struct png png;
	int fd;
	char buf[64];
	char * idat, * p, * out = NULL;
	size_t idatsz, idatoff, outsz;
	uint32_t l, i, j, off, sll;
	ssize_t ret;
	z_stream stream;

	fd = open(fn, O_RDONLY);
	if (fd < 0) return -1;

	/* PNG header */
	xread(fd, buf, 8);
	if (memcmp(buf, "\x89PNG\x0d\x0a\x1a\x0a", 8)) return -1;

	/* first chunk should be IHDR */
	xread(fd, buf, 8);
	if (memcmp(buf+4, "IHDR", 4)) return -1;
	l = ntohl(*(uint32_t *)buf);
	if (l != 13) return -1;
	xread(fd, buf, l);

	/* get width, height and color type */
	png.w = ntohl(*(uint32_t *)buf);
	png.h = ntohl(*(uint32_t *)(buf+4));
	png.ct = *(buf+9);

	/* if it's not RGB+Alpha bail out */
	if (png.ct != 6) return -1;

	/* skip CRC */
	lseek(fd, 4, SEEK_CUR);

	/* IDAT structure */
	idatsz = 4096;
	idatoff = 0;
	idat = malloc(idatsz);
	if (!idat) return -1;

	/* loop until all IDAT chunks are parsed */
	while (1) {
		memset(buf, 0, sizeof(buf));
		xread(fd, buf, 8);
		l = ntohl(*(uint32_t *)buf);
		if (!memcmp(buf+4, "IDAT", 4)) {
			if (idatoff + l < idatoff || idatoff + l < l) {
				fatal("integer overflow");
			}
			while ((idatoff + l) > idatsz) {
				idatsz <<= 2;
				p = realloc(idat, idatsz);
				if (!p) pfatal("realloc");
				idat = p;
			}

			xread(fd, idat+idatoff, l);
			idatoff += l;
			
			/* skip CRC */
			ret = lseek(fd, 4, SEEK_CUR);
			if (ret < 0) pfatal("lseek");


		}
		else if (!memcmp(buf+4, "IEND", 4)) {
			break;
		}
		else {
			/* skip chunk + CRC */
			ret = lseek(fd, l+4, SEEK_CUR);
			if (ret < 0) pfatal("lseek");
		}
	}	
	
	outsz = (png.w+1)*(png.h-1)*4;
	out = malloc(outsz);
	if (!out) pfatal("malloc");

	/* decompress IDAT stream */
	memset(&stream, 0, sizeof(stream));
	stream.next_in = (unsigned char *)idat;
	stream.avail_in = idatoff;
	stream.next_out = (unsigned char *)out;
	stream.avail_out = outsz;
	ret = inflateInit(&stream);
	if (ret != Z_OK) goto err;
        ret = inflate(&stream, Z_NO_FLUSH);
	if (ret != Z_OK && ret != Z_STREAM_END) goto err;
	ret = inflateEnd(&stream);
	if (ret != Z_OK) goto err;

	/* apply filters */
	sll = png.w*4; /* scanline length */
	for (i=0;i<png.h-1;i++) {
		off = (i*sll)+i;

		/* need to implement these PNG filters correctly */
		switch (out[off]) {
		case 0:
			for (j=0;j<sll;j++) out[(i*sll)+j]=out[off+1+j];
			break;
		case 1:
			for (j=0;j<sll;j++) {
				l = (j < 4 ? 0 : out[(i*sll)+j-4]);
				out[(i*sll)+j]=((out[off+1+j]+l)%256);
			}
			break;
		case 2:
			for (j=0;j<sll;j++) out[(i*sll)+j]=out[off+1+j];
			break;
		case 3:
			for (j=0;j<sll;j++) out[(i*sll)+j]=out[off+1+j];
			break;
		case 4:
			break;
		default:
			goto err;
		}
	}
	
	png.px = (unsigned char *)out;
	memcpy(res, &png, sizeof(*res));

	free(idat);
	return 0;
err:
	if (*out) free(out);
	free(idat);
	return -1;
	
}

static int
gray_and_resize(struct png * png, uint32_t w, uint32_t h, uint8_t ** res)
{
	double w_ratio, h_ratio, px, py;
	uint32_t i, j, k, off, gp;
	uint8_t * respx;

	if (!png || !*res) return -1;

	w_ratio = ((double)(png->w)/((double)w));
	h_ratio = ((double)(png->h)/((double)h));

	respx = malloc(w*h);
	if (!respx) return -1;
	*res = respx;

	for (i=0;i<h;i++) {
		for (j=0;j<w;j++) {
			px = floor(j*w_ratio);
			py = floor(i*h_ratio);	
			off = (uint32_t)((py*png->w*4)+(px*4));

			/* calculate gray pixel by averaging RGB pixels */
			gp = 0;
			for (k=0;k<3;k++) {
				gp += png->px[off+k];
			}
			gp /= 3;
			*respx++ = gp;
		}
	}

	return 0;
}

static inline int
dhash(struct png * png, uint64_t * hash)
{
	uint64_t res;
	uint32_t off;
	uint8_t * px;
	int ret, i, j;

	if (!png || !hash) return -1;

	ret = gray_and_resize(png, 9, 8, &px);
	if (ret < 0) fatal("error while graying/resizing pixels");

	res = 0;
	for (i=0;i<8;i++) {
		for (j=0;j<8;j++) {
			off = (i*9+j);
			res = (res<<1) | (px[off] < px[off+1] ? 1 : 0);
		}
	}
	free(px);

	*hash = res;
	return 0;
}

static inline int
ahash(struct png * png, uint64_t * hash)
{
	uint64_t res;
	uint32_t off, mc;
	uint8_t * px;
	int ret, i, j;

	if (!png || !hash) return -1;

	ret = gray_and_resize(png, 8, 8, &px);
	if (ret < 0) fatal("error while graying/resizing pixels");

	/* calculate mean color */
	mc = 0;
	for (i=0;i<8;i++) {
		for (j=0;j<8;j++) {
			mc += px[i*8+j];
		}
	}
	mc /= 64;

	res = 0;
	for (i=0;i<8;i++) {
		for(j=0;j<8;j++) {
			off = (i*8+j);
			res = (res<<1 | (px[off] < mc ? 1 : 0));
		}
	}
	free(px);

	*hash = res;
	return 0;
}

static inline void
hash_file(const char * fn, uint64_t * d, uint64_t * a)
{
	struct png png;
	int ret;
	ret = png_parse_file(fn, &png);	
	if (ret < 0) fatal("error while parsing PNG file");
	ret = dhash(&png, d);
	if (ret < 0) fatal("error while calculating dhash");
	ret = ahash(&png, a);
	if (ret < 0) fatal("error while calculating ahash");
	free(png.px);
}

static inline uint64_t
hamming(uint64_t i, uint64_t j)
{
	uint64_t c, r;
	int k;

	r = i ^ j;
	c = 0;
	for (k=0;k<64;k++) {
		c = c + ((r>>k)&0x1);
	}
	return c;
}

static inline void
usage(const char * arg0)
{
	fprintf(stderr, "%s <file1.png> [file2.png]\n", arg0);
	fprintf(stderr, "This tool calculates perceptual hashes ");
	fprintf(stderr, "for PNG files. It only works for PNG files in\n");
	fprintf(stderr, "RGB+A format\n\n");
	fprintf(stderr, "When two PNG files are supplied it will also");
	fprintf(stderr, " output the hamming distance between the hashes\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char ** argv)
{
	uint64_t d1, a1, d2, a2;

	printf("pnghash -- gvb@santarago.org\n\n"); 

	if (argc < 2) usage(argc > 0 ? argv[0] : "(unknown)");
	if (argc > 3) usage(argv[0]);

	hash_file(argv[1], &d1, &a1);
	printf("0x%.16zx 0x%.16zx\n", d1, a1);

	if (argc == 3) {
		hash_file(argv[2], &d2, &a2);
		printf("0x%.16zx 0x%.16zx\n", d2, a2);

		printf("%.2zu                 %.2zu\n", 
			hamming(d1, d2),
			hamming(a1, a2)
		);
	}

	exit(EXIT_SUCCESS);
}
