CFLAGS=-Ofast -march=native -flto -Wall -Wextra -Werror -Wconversion -Wwrite-strings -Wpadded -g
LDLIBS=-lglfw -lGLEW -lGL

all: wuimg
clean:
	rm -f *.o wuimg
.PHONY: all clean
