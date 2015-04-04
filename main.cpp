#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <FreeImage.h>
#include "svm.h"


extern "C" double *fext_color_layout(FIBITMAP *dib, int subpx, int *out_len);

extern "C" double *fext_texture(
	FIBITMAP *dib, int threshold_steps, double *scales, int n_scales, 
	int *out_len);


FIBITMAP *rescale_image(FIBITMAP *original_dib, int target_width)
{
	double rescale_factor = 
		((double) target_width) / FreeImage_GetWidth(original_dib);
	FIBITMAP *dib = FreeImage_Rescale(original_dib, 
		target_width, rescale_factor * FreeImage_GetHeight(original_dib),
		FILTER_CATMULLROM);
	return dib;
}


int main(int argc, char *argv[])
{
    if (argc == 2)
	{
		const char *image_file = argv[1];
		FIBITMAP *original_dib = FreeImage_Load(FIF_JPEG, image_file, 0);
		if (original_dib != NULL) {
			// rescale image to 300 * y
			FIBITMAP *dib = rescale_image(original_dib, 300);
			int threshold_steps = 20;
			double scales[] = {1.0, 0.5, 0.25, 0.125, 0.0625};
			int len_vec0 = 0, len_vec1 = 0;
			double *vec0 = fext_color_layout(dib, 8, &len_vec0);
			double *vec1 = fext_texture(
				dib, threshold_steps, scales, sizeof(scales)/sizeof(double), 
				&len_vec1);

			// convert vec0 and vec1 to a vector of svm_node
			struct svm_node *vecnode = (struct svm_node *) malloc(
				sizeof(struct svm_node) * (len_vec0 + len_vec1 + 1));
			int k = 0, i = 0;
			for (i = 0; i < len_vec0; k++, i++) {
				vecnode[k].index = k + 1;
				vecnode[k].value = vec0[i];
			}
			for (i = 0; i < len_vec1; k++, i++) {
				vecnode[k].index = k + 1;
				vecnode[k].value = vec1[i];
			}
			vecnode[k].index = -1; // endmark
			vecnode[k].value = 0;

			// printf("vector size: %d\n", k);
			// for (i = 0; i < k; i++) {
			// 	printf("%d:%g ", vecnode[i].index, vecnode[i].value);
			// }
			// printf("\n");

			// do svm-predict
			svm_model *model = svm_load_model("./comicops.model");
			if (model != NULL) {
				double res = svm_predict(model, vecnode);
				printf("%s\n", res > 0? "suspicious": "safe");
			}
			else {
				fprintf(stderr, "Could not load svm model, "
					"please check your package integrity!\n");
			}

			// done
			free(vecnode);
			free(vec0);
			free(vec1);
			svm_free_and_destroy_model(&model);
			FreeImage_Unload(dib);
			FreeImage_Unload(original_dib);
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
