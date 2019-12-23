CC = gcc
CFLAGS = -Ofast -march=native -flto -Wall -Wextra -Werror -Wconversion -Wwrite-strings -g #-s
LDLIBS = -lm -lepoxy -lglfw -lpng -ljpeg -lgif -ltiff -lopenjp2 -lwebp -lwebpdemux -lheif -lflif

OBJS = main.o dec.o anim_common.o common.o opengl.o window.o wudefs.o
INTERNAL_DECODERS = dec_pi.o dec_netpbm.o
ANIM_DECODERS = dec_gif.o dec_webp.o
DECODERS = $(INTERNAL_DECODERS) $(ANIM_DECODERS) dec_png.o dec_jpeg.o dec_tiff.o dec_jpeg2000.o dec_heif.o dec_flif.o
INTERNAL_LIBS = lib_pi.o lib_netpbm.o
COMMON = common.h wudefs.h

wu: $(OBJS) $(DECODERS) $(INTERNAL_LIBS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)
main.o: dec.h opengl.h window.h $(COMMON)
dec.o: $(DECODERS:.o=.h) wudefs.h
$(DECODERS): $(COMMON)
$(ANIM_DECODERS): anim_common.h
$(INTERNAL_DECODERS): $(INTERNAL_LIBS:.o=.h)
lib_pi.o: $(COMMON)
window.o: window.h opengl.h $(COMMON)
opengl.o: opengl.h $(COMMON)
anim_common.o: wudefs.h
common.o: $(COMMON)
wudefs.o: wudefs.h
clean:
	rm -f wu *.o
.PHONY: clean
