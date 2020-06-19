shell = /bin/sh
CFLAGS = -Ofast -march=native -flto -Wall -Wextra -Werror -Wconversion -Wwrite-strings -Winline -g #-fanalyzer
LDLIBS = -lm -lepoxy -lglfw -lpng -ljpeg -lgif -ltiff -lopenjp2 -lwebp -lwebpdemux -lheif -lflif $(shell pkg-config --libs librsvg-2.0)

INTERNAL_DECODERS = dec_bmp.o dec_pi.o dec_pnm.o dec_sgi.o dec_sun.o dec_tga.o dec_wbmp.o

INTERNAL_LIBS = $(INTERNAL_DECODERS:dec_%=lib_%)
DECODERS = $(INTERNAL_DECODERS) dec_png.o dec_jpeg.o dec_gif.o dec_tiff.o dec_jpeg2000.o dec_webp.o dec_heif.o dec_flif.o dec_svg.o

COMMON = common.h common_unpack.h common_composite.h

OBJS = dec.o window.o opengl.o $(DECODERS) $(INTERNAL_LIBS) $(COMMON_ALL:.h=.o)


.PHONY: all clean

wu: main.o $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)
wu_bench: gl_upload_bench.o dec_pnm.o lib_pnm.o window.o opengl.o common.o common_unpack.o wudefs.o
	$(CC) -o $@ $^ $(CFLAGS) -lm -lepoxy -lglfw

main.o: dec.h window.h opengl.h common.h wudefs.h conf.c
dec.o: $(DECODERS:.o=.h) common.h wudefs.h
window.o: opengl.h common.h wudefs.h
opengl.o: common.h wudefs.h
common_composite.o: wudefs.h

$(INTERNAL_DECODERS): $(INTERNAL_LIBS:.o=.h) $(COMMON)
$(INTERNAL_LIBS): $(COMMON)
$(DECODERS): $(COMMON) wudefs.h
dec_svg.o: dec_svg.c
	$(CC) -c $< $(CFLAGS) $(shell pkg-config --cflags librsvg-2.0)

all: wu wu_bench
clean:
	rm -f wu *.o
