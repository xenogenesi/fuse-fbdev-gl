CC=gcc

lib_FBDRV = libfbdrv.so
lib_XTTY = fbdrv-ttyx.so

all: $(lib_FBDRV) $(lib_XTTY) glfbe

.PHONY: clean
clean:
	-rm -f *~ $(lib_FBDRV) $(FBDRV_OBJECTS) $(lib_XTTY) $(XTTY_OBJECTS) glfbe.o glfbe

#X64 = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS = -O0 -g -Wall -W --shared -fPIC $(X64)

FBDRV_SOURCES = fbdrv.c
FBDRV_OBJECTS = $(FBDRV_SOURCES:.c=.o)

XTTY_SOURCES = fbdrv_ttyx.c
XTTY_OBJECTS = $(XTTY_SOURCES:.c=.o)

GLFBE_SOURCES = glfbe.c
GLFBE_OBJECTS = $(GLFBE_SOURCES:.c=.o)

#-Wl,-soname -Wl,$(lib_FBDRV)
FBDRV_LDFLAGS = -ldl
XTTY_LDFLAGS = -ldl

$(lib_FBDRV): $(FBDRV_OBJECTS)
	$(CC) -shared $(FBDRV_OBJECTS) -o $@ $(FBDRV_LDFLAGS)
$(lib_XTTY): $(XTTY_OBJECTS)
	$(CC) -shared $(XTTY_OBJECTS) -o $@ $(XTTY_LDFLAGS)

glfbe: $(GLFBE_OBJECTS)
	$(CC) $(GLFBE_OBJECTS) -o $@ -lGL -lGLU -lglut

.c.o:
	$(CC) $(CFLAGS) -c $^ -o $@

