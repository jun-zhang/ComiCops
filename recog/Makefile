FREEIMAGE_INC = ../freeimage/include/
FREEIMAGE_LIB = ../freeimage/lib/
CXXFLAGS += -I$(FREEIMAGE_INC) -L$(FREEIMAGE_LIB) -lfreeimage -g -Wall -Wextra -Wno-unused-function -lm -O3 -fopenmp

recog: main.cpp svm.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) -L../featureext/ -lcolor_layout -ltexture

.PHONY: clean
clean:
	rm -f recog
