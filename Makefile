CFLAGS = -Ofast -march=native -g -flto \
	-Wall -Wextra -Werror -Wconversion -Wwrite-strings -Winline \
	-Wredundant-decls -Wno-aggressive-loop-optimizations \
	-Wpointer-arith -Wunsafe-loop-optimizations \
	-Wtrampolines -Wnull-dereference -Wformat-signedness \
	#-fanalyzer
CLI_LDLIBS = -lm -luchardet -larchive \
	-lpng -ljpeg -lgif -ltiff -ljbig -lopenjp2 -lwebp -lwebpdemux -lheif \
	-lflif -lraw $(shell pkg-config --libs librsvg-2.0)
LDLIBS = $(CLI_LDLIBS) -lepoxy -lglfw

INTERNAL_DECODERS = dec_avs.o dec_bmp.o dec_pcx.o dec_pi.o dec_pnm.o \
	dec_sgi.o dec_sun.o dec_tga.o dec_wbmp.o dec_xbm.o
EXTERNAL_DECODERS = dec_flif.o dec_gif.o dec_heif.o dec_jbig.o dec_jpeg.o \
	dec_jpeg2000.o dec_png.o dec_raw.o dec_svg.o dec_tiff.o dec_webp.o

DECODERS = $(INTERNAL_DECODERS) $(EXTERNAL_DECODERS)
INTERNAL_LIBS = $(INTERNAL_DECODERS:dec_%=lib_%)

COMMON = common.h common_unpack.h common_composite.h

CLI_OBJS = extract.o write_pam.o display.o window.o opengl.o events.o dec.o \
	$(DECODERS) $(INTERNAL_LIBS) $(COMMON:.h=.o) common_lib.o \
	colorimetry.o wudefs.o
OBJS = $(CLI_OBJS) display.o window.o opengl.o


.PHONY: all clean

wu: main.o $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS) $(CFLAGS)
wu_bench: gl_upload_bench.o $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS) $(CFLAGS)
wuvib: wuvib.o $(CLI_OBJS)
	$(CC) -o $@ $^ $(CLI_LDLIBS) $(CFLAGS)

main.o: extract.h write_pam.h display.h dec.h common.h wudefs.h conf.c
write_pam.o: write_pam.h common_unpack.h
display.o: display.h window.h opengl.h colorimetry.h events.h dec.h wudefs.h
window.o: window.h opengl.h events.h common.h wudefs.h
opengl.o: opengl.h common.h wudefs.h
events.o: events.h common.h wudefs.h
extract.o: extract.h dec.h common.h
dec.o: dec.h dec.def $(DECODERS:.o=.h) common.h wudefs.h
common_composite.o: common_composite.h wudefs.h

$(INTERNAL_DECODERS): dec_%.o: lib_%.h $(COMMON) wudefs.h
$(INTERNAL_LIBS): $(COMMON) common_lib.h
$(EXTERNAL_DECODERS): $(COMMON) wudefs.h
dec_raw.o: dec_jpeg.h
dec_svg.o: dec_svg.c
	$(CC) -c $< $(CFLAGS) $(shell pkg-config --cflags librsvg-2.0)

all: wu wu_bench wuvib
clean:
	rm -f wu *.o
