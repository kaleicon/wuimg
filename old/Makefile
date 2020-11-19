CFLAGS = -Ofast -march=native -g -flto -std=gnu11 \
	-Wall -Wextra -Werror -Wconversion -Wwrite-strings -Winline \
	-Wpedantic -Wformat-signedness  -Wno-aggressive-loop-optimizations \
	-Wunsafe-loop-optimizations \
	#-fanalyzer
BASE_LDLIBS = -lm -luchardet -larchive \
	-lpng -ljpeg -lgif -ltiff -ljbig -lopenjp2 -lwebp -lwebpdemux \
	-lheif -lflif -lraw $(shell pkg-config --libs librsvg-2.0)
LDLIBS = $(BASE_LDLIBS) -lepoxy -lglfw

INTERNAL_FMTS = avs bmp pcx pgx pi pnm sgi sun tga wbmp xbm
EXTERNAL_FMTS = flif gif heif jbig jpeg jpeg2000 png raw svg tiff webp

INTERNAL_DECODERS = $(INTERNAL_FMTS:%=dec/%.o)
EXTERNAL_DECODERS = $(EXTERNAL_FMTS:%=dec/%.o)

DECODERS = $(INTERNAL_DECODERS) $(EXTERNAL_DECODERS)
INTERNAL_LIBS = $(INTERNAL_FMTS:%=dec/lib/%.o)

COMMON = common.h dec/lib/common/unpack.h dec/lib/common/composite.h

BASE_OBJS = extract.o write_pam.o dec.o $(DECODERS) $(INTERNAL_LIBS) \
	$(COMMON:.h=.o) common_lib.o colorimetry.o conf.o wudefs.o
GUI_OBJS = display.o window.o opengl.o events.o
OBJS = $(BASE_OBJS) $(GUI_OBJS)


.PHONY: all clean

wu: main.o $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS) $(CFLAGS)
wu_bench: gl_upload_bench.o $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS) $(CFLAGS)
wuvib: wuvib.o $(BASE_OBJS)
	$(CC) -o $@ $^ $(BASE_LDLIBS) $(CFLAGS)

main.o: extract.h write_pam.h display.h dec.h common.h wudefs.h conf.h
write_pam.o: write_pam.h dec/lib/common/unpack.h
display.o: display.h window.h opengl.h colorimetry.h events.h dec.h wudefs.h
window.o: window.h opengl.h events.h common.h wudefs.h
opengl.o: opengl.h common.h wudefs.h
events.o: events.h common.h wudefs.h
extract.o: extract.h dec.h common.h
dec.o: dec.h dec.def $(DECODERS:.o=.h) common.h wudefs.h
common_composite.o: dec/lib/common/composite.h wudefs.h

$(INTERNAL_DECODERS): dec/%.o: dec/lib/%.h $(COMMON) wudefs.h
$(INTERNAL_LIBS): $(COMMON) dec/lib/common/lib.h
$(EXTERNAL_DECODERS): $(COMMON) wudefs.h
dec_raw.o: dec/jpeg.h
dec_svg.o: dec/svg.c
	$(CC) -c $< $(CFLAGS) $(shell pkg-config --cflags librsvg-2.0)

all: wu wu_bench wuvib
clean:
	rm -f wu *.o
