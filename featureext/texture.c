#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <FreeImage.h>


/* 
 * Read pixels from FreeImage DIB handle and convert the RGB pixels to grayscale
 * values (range 0 .. 255). We use a simple weighted sum identical with Luma coding
 * in video systems
 */
static unsigned char *dib_to_grayscale(FIBITMAP *dib, int *out_width, int *out_height)
{
	int width = FreeImage_GetWidth(dib);
	int height = FreeImage_GetHeight(dib);
	
	unsigned char *bitmap = malloc(width * height);
	unsigned char *line = bitmap;
	int x, y;
	for (y = height - 1; y >= 0; y--, line += width) {
		for (x = 0; x < width; x++) {
			RGBQUAD rgb;
			FreeImage_GetPixelColor(dib, x, y, &rgb);
			/* converting RGB to grayscale */
			line[x] = 0.299 * rgb.rgbRed   + 0.587 * rgb.rgbGreen   + 0.114 * rgb.rgbBlue;
		}
	}

	*out_width = width;
	*out_height = height;
	return bitmap;
}


/* 
 * Convert grayscale image to binary image, using specified threshold
 */
static void grayscale_to_binmap(
	unsigned char *grayscale, int width, int height, int threshold)
{
	unsigned char *p = grayscale;
	int n_px = width * height;
	while (n_px-- != 0) {
		*p = (*p > threshold? 1: 0);
		p++;
	}
}


/* 
 * A simple and fast feature extraction formular is applied to specified binary
 * bitmap:
 *
 * F = [sum |N(i,j) * tmpmat|]/(M * N), 
 *     where N(i,j) is the 3x3 neighbourhood centered at (i,j)
 *           i in [0 .. M), j in [0 .. N)
 *           M, N is the image width, height
 */
static double convolve(unsigned char *binmap, int width, int height, int tmpmat[3][3])
{
	unsigned char *line = binmap;
	int cx, cy, sum = 0;
	for (cy = 1; cy < height - 1; cy++, line += width) {
		unsigned char
			*px0 = line,
			*px1 = px0 + width,
			*px2 = px1 + width;
#pragma omp parallel for private(cx) reduction(+:sum)
		for (cx = 1; cx < width - 1; cx++) {
			int l = cx - 1, m = cx, r = cx + 1;
			int val =
				px0[l] * tmpmat[0][0] + px0[m] * tmpmat[0][1] + px0[r] * tmpmat[0][2] +
				px1[l] * tmpmat[1][0] + px1[m] * tmpmat[1][1] + px1[r] * tmpmat[1][2] +
				px2[l] * tmpmat[2][0] + px2[m] * tmpmat[2][1] + px2[r] * tmpmat[2][2];
			val = (val > 0? val: -val);
			sum += val;
		}
	}
	return sum / (double)(width * height);
}


/* 
 * Do feature extraction with specified threshold and tmpmat
 */
static double extract_feature(
	unsigned char *bitmap, int width, int height, int threshold, int tmpmat[3][3])
{
	double val;
	int n_px = width * height;
	unsigned char *bin = malloc(n_px);
	memcpy(bin, bitmap, n_px);
	grayscale_to_binmap(bin, width, height, threshold);
	val = convolve(bin, width, height, tmpmat);
	free(bin);
	return val;
}

static void do_extraction(
	unsigned char *bitmap, int width, int height, int threshold_steps, 
	int tmpmat[3][3], 
	double *out_vec)
{
	int delta = 256 / threshold_steps;
	int threshold = delta;
	int i = 0;
	for ( ; i < threshold_steps; i++, threshold += delta) {
		out_vec[i] = extract_feature(bitmap, width, height, threshold, tmpmat);
	}
}

/* 
 * Rescale the image to different sizes and do the extraction for each rescaled
 * version. We extract features on multiple scaling in order to obtain texture info
 * on various level (from fine to coarse)
 *
 * size of out_vec should be at least n_scales * threshold_steps
 */
static void do_extraction_multiscale(
	FIBITMAP *dib, double *scales, int n_scales,
	int threshold_steps, int tmpmat[3][3],
	double *out_vec)
{
	int i = 0;
	for ( ; i < n_scales; i++)
	{
		int width = FreeImage_GetWidth(dib);
		int height = FreeImage_GetHeight(dib);
		int dst_width = width * scales[i];
		int dst_height = height * scales[i];
		FIBITMAP *resc_dib = FreeImage_Rescale(
			dib, dst_width, dst_height, FILTER_CATMULLROM);
		unsigned char *grays = dib_to_grayscale(resc_dib, &width, &height);

		do_extraction(grays, width, height, threshold_steps, tmpmat, 
			&out_vec[i*threshold_steps]);

		free(grays);
		FreeImage_Unload(resc_dib);
	}
}


double *fext_texture(
	FIBITMAP *dib, int threshold_steps, double *scales, int n_scales, 
	int *out_len)
{
	int tmpmatA[3][3] = {
		{1, 0, -1},
		{1, 0, -1},
		{1, 0, -1} };
	int tmpmatB[3][3] = {
		{-1, -1, -1},
		{0, 0, 0},
		{1, 1, 1} };

	int len = n_scales * threshold_steps;
	double *fvec = calloc(sizeof(double), len * 2);
	do_extraction_multiscale(dib, scales, n_scales, threshold_steps, tmpmatA, fvec);
	do_extraction_multiscale(dib, scales, n_scales, threshold_steps, tmpmatB, 
		&fvec[len]);

	*out_len = 2 * len;
	return fvec;
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
			double scales[] = {1.0, 0.5, 0.25, 0.125, 0.0625};
			int threshold_steps = 20;
			double *vec = fext_texture(
				dib, threshold_steps, 
				scales, sizeof(scales)/sizeof(double),
				&veclen);
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
