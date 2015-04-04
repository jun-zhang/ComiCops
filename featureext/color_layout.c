#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <FreeImage.h>



/* 
 * In order to model the color layout of images more close with human sense, we
 * convert RGB pixels to YCbCr color space, which will make better sense for image
 * recognition and classification
 */
struct pixel_ycbcr {
    double y, cb, cr;
};

/* 
 * Convert between YUV and RGB according to SDTV with BT.601
 */
static void rgb2ycbcr(int r, int g, int b, struct pixel_ycbcr *p)
{
	p->y  =  0.299 * r   + 0.587 * g   + 0.114 * b;
	p->cb = -0.14713 * r - 0.28886 * g + 0.436 * b;
	p->cr =  0.615 * r   - 0.51499 * g - 0.10001 * b;
}

static void ycbcr2rgb(int *r, int *g, int *b, struct pixel_ycbcr *p)
{
	double y = p->y, cb = p->cb, cr = p->cr;
	*r = y                + 1.13983 * cr;
	*g = y - 0.39465 * cb - 0.58060 * cr;
	*b = y + 2.03211 * cb;
}


/* S[u,v] = 1/4 * C[u] * C[v] *
 *   sum for x=0 to width-1 of
 *   sum for y=0 to height-1 of
 *     s[x,y] * cos( (2x+1)*u*PI / 2N ) * cos( (2y+1)*v*PI / 2N )
 *
 * C[u], C[v] = 1/sqrt(2) for u, v = 0
 * otherwise, C[u], C[v] = 1
 *
 * S[u,v] ranges from -2^10 to 2^10
 */

#define COEFFS(Cu,Cv,u,v) {									\
		if (u == 0) Cu = 1.0 / sqrt(2.0); else Cu = 1.0;	\
		if (v == 0) Cv = 1.0 / sqrt(2.0); else Cv = 1.0;	\
	}

/* output := DCT(input)  */
static void dct(double input[8][8], double output[8][8])
{
	int u,v,x,y;
	for (v = 0; v < 8; v++)
		for (u = 0; u < 8; u++) {
			double Cu, Cv, z = 0.0;
			COEFFS(Cu,Cv,u,v);
			for (y = 0; y < 8; y++) {
				for (x = 0; x < 8; x++) {
					double s, q;
					s = input[x][y];
					q = s *
						cos((double)(2*x+1) * (double)u * M_PI/16.0) *
						cos((double)(2*y+1) * (double)v * M_PI/16.0);
					z += q;
				}
			}
			output[v][u] = 0.25 * Cu * Cv * z;
		}
}

/* output := IDCT(input) */
static void idct(double input[8][8], double output[8][8])
{
	int u,v,x,y;
	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			double z = 0.0;
			for (v = 0; v < 8; v++) {
				for (u = 0; u < 8; u++) {
					double S, q;
					double Cu, Cv;
					COEFFS(Cu,Cv,u,v);
					S = input[v][u];
					q = Cu * Cv * S *
						cos((double)(2*x+1) * (double)u * M_PI/16.0) *
						cos((double)(2*y+1) * (double)v * M_PI/16.0);
					z += q;
				}
			}
			z /= 4.0;
			if (z > 255.0) z = 255.0;
			if (z < 0) z = 0.0;
			output[x][y] = z;
		}
	}
}


/* 
 * Read pixels from FreeImage DIB handle and convert the pixels of this image from
 * RGB color space to YCbCr.
 *
 * A buffer containing YCbCr pixels will be returned; the width and height of the
 * original image would be returned from output arguments of this function
 */
static struct pixel_ycbcr *dib_to_ycbcr(FIBITMAP *dib, int *out_width, int *out_height)
{
	int width = FreeImage_GetWidth(dib);
	int height = FreeImage_GetHeight(dib);

	/* convert rgb to ycbcr pixel by pixel */
	struct pixel_ycbcr *ycbcr = malloc(sizeof(struct pixel_ycbcr) * width * height);
	struct pixel_ycbcr *line = ycbcr;
	int x, y;
	for (y = height - 1; y >= 0; y--, line += width) {
#pragma omp parallel for private(x)
		for (x = 0; x < width; x++) {
			RGBQUAD rgb;
			FreeImage_GetPixelColor(dib, x, y, &rgb);
			rgb2ycbcr(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue, &line[x]);
		}
	}

	*out_width = width;
	*out_height = height;
	return ycbcr;
}


/* 
 * Subsampling the YCbCr image to reduce the amount of irrelavant features while
 * keeping the overall color layout relatively intact, calculations required by DCT
 * transformation.
 */
static struct pixel_ycbcr *subsampling_ycbcr(
	struct pixel_ycbcr *ycbcr, int width, int height,
	int sub_width, int sub_height)
{
	int block_w = width / sub_width;
	int block_h = height / sub_height;
	int n_blkpx = block_w * block_h;
	double f_blkpx = 1.0 / n_blkpx;

	/* 
	 * do the subsampling, we made best effort to access the original large ycbcr
	 * buffer sequentially to preseve locality thus increase cache hit rates, thus
	 * obtain better performance
	 */
	struct pixel_ycbcr *subycbcr = calloc(
		sizeof(struct pixel_ycbcr), sub_width * sub_height);
	struct pixel_ycbcr *subline = subycbcr;
	struct pixel_ycbcr *line = ycbcr;
	int sx, y, sy = 0;
	for (y = 0; y < height; y++, line += width)
	{
		/* scan a line of pixels and merge each block_w of pixels as one subpixel */
		struct pixel_ycbcr *p = line, *sp = subline;
		for (sx = 0; sx < sub_width; sx++) {
			double y = 0, cb = 0, cr = 0;
			int i = 0;
			for ( ; i < block_w; i++) {
				y += p->y;
				cb += p->cb;
				cr += p->cr;
				p++;
			}
			sp->y += y;
			sp->cb += cb;
			sp->cr += cr;
			sp++;
		}

		/* merge block_hs of horizontal strides into one line of subpixels */
		if ((y + 1) % block_h == 0) {
			struct pixel_ycbcr *sp = subline;
			for (sx = 0; sx < sub_width; sx++, sp++) {
				sp->y *= f_blkpx;
				sp->cb *= f_blkpx;
				sp->cr *= f_blkpx;
			}
			subline += sub_width;
			if (++sy == sub_height) {
				break;
			}
		}
	}

	return subycbcr;
}


/* 
 * Perform an inplace DCT on YCbCr image, we simply apply DCT to each component of
 * the pixels separately.
 */
static void ycbcr_dct(struct pixel_ycbcr *ycbcr)
{
	double y_data[8][8], cb_data[8][8], cr_data[8][8];
	double y_dct[8][8], cb_dct[8][8], cr_dct[8][8];
	int x, y;

	/* load pixels to temporary buffer */
	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			y_data[x][y] = ycbcr[x + (y << 3)].y;
			cb_data[x][y] = ycbcr[x + (y << 3)].cb;
			cr_data[x][y] = ycbcr[x + (y << 3)].cr;
		}
	}

	/* do dct */
	dct(y_data, y_dct);
	dct(cb_data, cb_dct);
	dct(cr_data, cr_dct);

	/* write result back to ycbcr image */
	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			ycbcr[x + (y << 3)].y = y_dct[x][y];
			ycbcr[x + (y << 3)].cb = cb_dct[x][y];
			ycbcr[x + (y << 3)].cr = cr_dct[x][y];
		}
	}
}


/* 
 * Rearrange YCbCr image as a 1D vector in zig-zag order
 */
static struct pixel_ycbcr *ycbcr_zigzag(struct pixel_ycbcr *ycbcr)
{
#define ZZ(k) zigzag[i++] = ycbcr[k]
	struct pixel_ycbcr *zigzag = malloc(sizeof(struct pixel_ycbcr) * 64);
	int i = 0;
	ZZ(0); ZZ(1); ZZ(8); ZZ(16); ZZ(9); ZZ(2); ZZ(3); ZZ(10); ZZ(17);
	ZZ(24); ZZ(32); ZZ(25); ZZ(18); ZZ(11); ZZ(4); ZZ(5); ZZ(12);
	ZZ(19); ZZ(26); ZZ(33); ZZ(40); ZZ(48); ZZ(41); ZZ(34); ZZ(27);
	ZZ(20); ZZ(13); ZZ(6); ZZ(7); ZZ(14); ZZ(21); ZZ(28); ZZ(35);
	ZZ(42); ZZ(49); ZZ(56); ZZ(57); ZZ(50); ZZ(43); ZZ(36); ZZ(29);
	ZZ(22); ZZ(15); ZZ(23); ZZ(30); ZZ(37); ZZ(44); ZZ(51); ZZ(58);
	ZZ(59); ZZ(52); ZZ(45); ZZ(38); ZZ(31); ZZ(39); ZZ(46); ZZ(53);
	ZZ(60); ZZ(61); ZZ(54); ZZ(47); ZZ(55); ZZ(62); ZZ(63);
	return zigzag;
}


/* 
 * Normalize vector to make all elements in range [0 .. 1]
 */
static void normalize_vector(double *vec, int len)
{
	if (len == 0) return;
	else {
		double max = vec[0];
		double fac = 1;
		int i = 1;
		for ( ; i < len; i++) max = (vec[i] > max? vec[i]: max);
		fac = 1 / max;
		for (i = 0; i < len; i++) vec[i] *= fac;
	}
}


/* 
 * Dump YCbCr buffer as P3 PPM image
 */
static void dump_ycbcr_as_ppm(struct pixel_ycbcr *ycbcr, int width, int height)
{	
	struct pixel_ycbcr *p = ycbcr;
	int n_px = width * height;

	printf("P3\n");
	printf("%d %d\n", width, height);
	printf("255\n");
	while (n_px-- != 0) {
		int r, g, b;
		ycbcr2rgb(&r, &g, &b, p++);
		printf("%d %d %d\n", r, g, b);
	}
}


/* 
 * Entry of color layout feature extraction routine. a subpx value of 8 is highly
 * recommended
 */
double *fext_color_layout(FIBITMAP *dib, int subpx, int *out_len)
{
	int sub_width = subpx, sub_height = subpx;
	int width, height;
	struct pixel_ycbcr *ycbcr = dib_to_ycbcr(dib, &width, &height);
	struct pixel_ycbcr *subycbcr = subsampling_ycbcr(
		ycbcr, width, height, sub_width, sub_height);
	struct pixel_ycbcr *zigzag;
	ycbcr_dct(subycbcr);
	zigzag = ycbcr_zigzag(subycbcr);
			
	/* flatten YCbCr vector as a real vector */
	double *vec = malloc(sizeof(double) * sub_width * sub_height * 3);
	int i = 0, k = 0;
	for ( ; i < sub_width * sub_height; i++) {
		vec[k++] = zigzag[i].y;
		vec[k++] = zigzag[i].cb;
		vec[k++] = zigzag[i].cr;
	}
	
	*out_len = k;
	normalize_vector(vec, sub_width * sub_height * 3);

	/* done */
	free(zigzag);
	free(subycbcr);
	free(ycbcr);
	return vec;
}


#ifdef FEXT_STANDALONE

int main(int argc, char *argv[])
{
    if (argc == 2)
	{
		const char *image_file = argv[1];
		FIBITMAP *dib = FreeImage_Load(FIF_JPEG, image_file, 0);
		if (dib != NULL) {
			int veclen = 0, i = 0;
			double *vec = fext_color_layout(dib, 8, &veclen);
			for (i = 0; i < veclen; i++) {
				printf("%f ", vec[i]);
			}
			printf("\n");
			free(vec);
			FreeImage_Unload(dib);
		}
		else {
			fprintf(stderr, "failed loading image file %s\n", image_file);
		}
	}
	else {
		printf("usage: %s filename.jpg\n", argv[0]);
	}
    return 0;
}

#endif
