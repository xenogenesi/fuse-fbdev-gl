/*
	Emulated Framebuffer memory
*/

#include <linux/fb.h>

#define MEMSIZE (1 << 21)

// these get mapped the bottom of the display memory
struct priv_registers {
	char id[8];
	unsigned char ver;
	unsigned int msize;
	unsigned int cmoffset;

	enum { RES_PAL, RES_NTSC, RES_640x480 } res;
	enum { DEP_8, DEP_16, DEP_24, DEP_32 } depth;
	enum { ST_OK, ST_ERROR, ST_INIT, ST_FORCE } status;
};

