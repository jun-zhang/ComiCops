TARGETS = color_layout texture

FREEIMAGE_INC = ../freeimage/include/
FREEIMAGE_LIB = ../freeimage/lib/
CFLAGS += -I$(FREEIMAGE_INC) -L$(FREEIMAGE_LIB) -lfreeimage -g -Wall -Wextra -Wno-unused-function -lm -O3 -fopenmp

all: $(TARGETS)

color_layout: color_layout.c
	$(CC) -o $@ $^ $(CFLAGS) -DFEXT_STANDALONE
	$(CC) -o lib$@.so $^ $(CFLAGS) --shared -fPIC

texture: texture.c
	$(CC) -o $@ $^ $(CFLAGS) -DFEXT_STANDALONE
	$(CC) -o lib$@.so $^ $(CFLAGS) --shared -fPIC

.PHONY: clean
clean:
	rm -f $(TARGETS) $(addprefix lib, $(addsuffix .so, $(TARGETS)))
