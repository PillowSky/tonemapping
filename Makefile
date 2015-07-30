CC			= g++
CFLAGS		= -std=c++0x -Wall -fopenmp -march=corei7-avx -O3 -I ~/tone -I ~/tone/pfs -I ~/tone/pfstmo -I ~/tone/exrio `pkg-config --cflags OpenEXR fftw3 Magick++`
LINKFLAGS	= -lfftw3_threads `pkg-config --libs OpenEXR fftw3 Magick++`
SRCS		= main.cpp pde.cpp pde_fft.cpp tmo_fattal02.cpp pfs/pfs.cpp pfs/pfsutils.cpp pfs/colorspace.cpp exrio/exrio.cpp
OBJS		= $(SRCS:.cpp=.o)
PROG		= main

all: $(SRCS) $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(INCFLAGS) $(LINKFLAGS)

.cpp.o:
	$(CC) $(CFLAGS) $< -c -o $@ $(INCFLAGS)

clean:
	rm $(OBJS) $(PROG)
