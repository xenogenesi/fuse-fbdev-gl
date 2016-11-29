#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <errno.h>

#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include <dlfcn.h>
#ifndef RTLD_NEXT
#define RTLD_NEXT ((void *) -1l)
#endif

#include "fbemem.h"

#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define DPRINTF( format, args... ) do { \
		fprintf( stderr, "[%d]:[%s] ", getpid(), __FUNCTION__ ); \
		fprintf( stderr, format, ## args ); \
	} while( 0 )

#else
#define DPRINTF( format, args... )
#endif

#define TRY( c, e ) { if( !(c) ) { errno = e; return -1; } }

#if 0
static struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};
#endif

static struct fb_fix_screeninfo fb_fix = {
		.id = "glfbe",
		.visual = FB_VISUAL_TRUECOLOR,
		.smem_len = MEMSIZE,
		.type =		FB_TYPE_PACKED_PIXELS,
		.accel =	FB_ACCEL_NONE,
		.line_length = 640*3,
};

static struct fb_var_screeninfo fb_var = {
	.xres = 640,
	.yres = 480,
	.bits_per_pixel = 24,
	.red = { .offset = 0, .length = 8, },
	.green = { .offset = 8, .length = 8, },
	.blue = { .offset = 16, .length = 8, },
};

static struct priv_registers* registers;

typedef int request_t;

static int drvfd = -1;

typedef int (* OPEN) ( const char *, int, mode_t );
typedef int (* IOCTL) ( int, request_t, void * );
typedef int (* CLOSE) ( int );

void set_res( void )
{
	if( 640 == fb_var.xres && 480 == fb_var.yres )
		registers->res = RES_640x480;
	else if( 720 == fb_var.xres && 576 == fb_var.yres )
		registers->res = RES_PAL;
}

void set_depth( void )
{
	switch( fb_var.bits_per_pixel )
	{
		case 16:
			registers->depth = DEP_16;
			break;
		case 24:
			registers->depth = DEP_24;
			break;
		case 32:
			registers->depth = DEP_32;
			break;
		default:
			break;
	}
}

void map_registers( void )
{
	if( !(registers = mmap( NULL, sizeof( struct priv_registers ), PROT_READ|PROT_WRITE, MAP_SHARED, drvfd, MEMSIZE ) ) )
		exit(1);
}

int open( const char *pathname, int flags, ... )
{
	static OPEN real_open = NULL;
	va_list args;
	mode_t mode;

	if( !real_open )
		real_open = (OPEN) dlsym( RTLD_NEXT, "open" );

	va_start( args, flags );
	mode = va_arg( args, mode_t );
	va_end( args );

	if( strcmp( pathname, getenv( "FRAMEBUFFER" ) ) )
		return real_open( pathname, flags, mode );

	drvfd = real_open( pathname, flags & ~O_TRUNC, mode );

	map_registers();

	DPRINTF( "open %s %d\n", pathname, drvfd );

	return drvfd;
}

int open64( const char *pathname, int flags, ... )
{
	static OPEN real_open64 = NULL;
	va_list args;
	mode_t mode;

	if( !real_open64 )
		real_open64 = (OPEN) dlsym( RTLD_NEXT, "open64" );

	va_start( args, flags );
	mode = va_arg( args, mode_t );
	va_end( args );

	if( strcmp( pathname, getenv( "FRAMEBUFFER" ) ) )
		return real_open64( pathname, flags, mode );

	drvfd = real_open64( pathname, flags & ~O_TRUNC, mode );

	map_registers();

	DPRINTF( "open64 %s %d\n", pathname, drvfd );

	return drvfd;
}

static int check_var(struct fb_var_screeninfo *var, struct fb_fix_screeninfo *fix )
{
	//struct tdfx_par *par = (struct tdfx_par *) info->par; 
	unsigned long lpitch;

	if ( var->bits_per_pixel != 16 &&
	    var->bits_per_pixel != 24 && var->bits_per_pixel != 32 ) {
		DPRINTF("depth not supported: %u\n", var->bits_per_pixel);
		return EINVAL;
	}

	if (var->xres != var->xres_virtual)
		var->xres_virtual = var->xres;

	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	if (var->xoffset) {
		DPRINTF("xoffset not supported\n");
		return EINVAL;
	}

	/* Banshee doesn't support interlace, but Voodoo4/5 and probably Voodoo3 do. */
	/* no direct information about device id now? use max_pixclock for this... */
	if (((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) ) {
		DPRINTF("interlace not supported\n");
		return EINVAL;
	}

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		DPRINTF("double not supported\n");
		return EINVAL;
	}

	var->xres = (var->xres + 15) & ~15; /* could sometimes be 8 */
	lpitch = var->xres * ((var->bits_per_pixel + 7)>>3);
  
	if (var->xres < 320 || var->xres > 640) {
		DPRINTF("width not supported: %u\n", var->xres);
		return EINVAL;
	}
  
	if (var->yres < 200 || var->yres > 480) {
		DPRINTF("height not supported: %u\n", var->yres);
		return EINVAL;
	}
  
	if (lpitch * var->yres_virtual > fix->smem_len) {
		var->yres_virtual = fix->smem_len/lpitch;
		if (var->yres_virtual < var->yres) {
			DPRINTF("no memory for screen (%ux%ux%u)\n",
			var->xres, var->yres_virtual, var->bits_per_pixel);
			return EINVAL;
		}
	}
  
// 	if (PICOS2KHZ(var->pixclock) > par->max_pixclock) {
// 		DPRINTF("pixclock too high (%ldKHz)\n",PICOS2KHZ(var->pixclock));
// 		return EINVAL;
// 	}

	switch(var->bits_per_pixel) {
		case 8:
			var->transp.length = 0;
			var->red.length = var->green.length = var->blue.length = 8;
			break;
		case 16:
			var->red.offset   = 11;
			var->red.length   = 5;
			var->green.offset = 5;
			var->green.length = 6;
			var->blue.offset  = 0;
			var->blue.length  = 5;
			var->transp.length = var->transp.offset = 0;
			break;
		case 24:
			var->red.offset=16;
			var->green.offset=8;
			var->blue.offset=0;
			var->red.length = var->green.length = var->blue.length = 8;
			var->transp.length = var->transp.offset = 0;
			break;
		case 32:
			var->transp.offset   = 24;
			var->red.offset   = 16;
			var->green.offset = 8;
			var->blue.offset  = 0;
			var->transp.length = var->red.length = var->green.length = var->blue.length = 8;
			break;
	}
	//var->height = var->width = -1;
  
	//var->accel_flags = FB_ACCELF_TEXT;
	
	DPRINTF("Checking graphics mode at %dx%d (%dx%d) depth %d %d\n",
		var->xres, var->yres, var->xres_virtual, var->yres_virtual,
		var->bits_per_pixel, var->vmode );

	return 0;
}

int ioctl( int fd, request_t request, ... )
{
	static IOCTL real_ioctl = NULL;
	va_list args;
	void *argp;

	if( !real_ioctl )
		real_ioctl = (IOCTL) dlsym( RTLD_NEXT, "ioctl" );

	va_start( args, request );
	argp = va_arg( args, void * );
	va_end( args );

	if( drvfd != -1 && fd != drvfd )
		return real_ioctl( fd, request, argp );

	switch( request )
	{
#if 0
		case KDSETMODE:
			//break;
#endif
		case VT_GETSTATE: {
/*
			int ret = -1;
			int fd = open("/dev/tty0", O_RDONLY );
			if( fd > -1 ) {
				ret = real_ioctl( fd, request, argp );
				close( fd );
			} else
				DPRINTF( "can't open /dev/tty0" );
			return ret;
*/		}
		case VT_ACTIVATE:
		case VT_WAITACTIVE:
		case VT_SETMODE:
		case VT_GETMODE:
			DPRINTF("request 0x%x\n", request);
			break;
		case VT_OPENQRY: /*{
			int ret = -1;
			int fd = open("/dev/tty0", O_RDONLY );
			if( fd > -1 ) {
				ret = real_ioctl( fd, request, argp );
				close( fd );
			} else
				DPRINTF( "can't open /dev/tty0" );
			return ret;
			}*/
			DPRINTF("VT_OPENQRY\n");
			*((int*) argp) = 7;
			break;
		case FBIOPUT_VSCREENINFO:
			DPRINTF("FBIOPUT_VSCREENINFO\n");
			if( check_var( (struct fb_var_screeninfo *) argp, &(fb_fix) ) )
				return 0;

			DPRINTF("   POST FBIOPUT_VSCREENINFO\n" );
			TRY( memcpy( &(fb_var), argp, sizeof(fb_var) ), EFAULT );

			DPRINTF("FBIOPUT_VSCREENINFO: %dx%dx%d %d%d%d%d\nxres_virtual=%d yres_virtual=%d\nxoffset=%d yoffset=%d\n", fb_var.xres, fb_var.yres, fb_var.bits_per_pixel, fb_var.red.length, fb_var.green.length, fb_var.blue.length,fb_var.transp.length, fb_var.xres_virtual, fb_var.yres_virtual, fb_var.xoffset,fb_var.yoffset );

			fb_fix.line_length = fb_var.xres * (fb_var.red.length + fb_var.green.length + fb_var.blue.length + fb_var.transp.length) >> 3;
			
			set_res(); set_depth();

			//msync( registers, sizeof(fb_var), MS_INVALIDATE );

			break;
		case FBIOGET_VSCREENINFO:
			TRY( memcpy(argp, &(fb_var), sizeof(fb_var) ), EFAULT );
			DPRINTF("FBIOGET_VSCREENINFO\n");
			break;
		case FBIOGET_FSCREENINFO:
			DPRINTF("FBIOGET_FSCREENINFO\n");
			TRY( memcpy(argp, &(fb_fix), sizeof(fb_fix) ), EFAULT );
			break;
		case FBIOPUTCMAP:
			DPRINTF( "FBIOPUTCMAP start=%d len=%d\n", ((struct fb_cmap*)argp)->start, ((struct fb_cmap*)argp)->len );
/*			registers->len = ((struct fb_cmap*)argp)->len;
			memcpy( registers->red, ((struct fb_cmap*)argp)->red, registers->len );
			memcpy( registers->green, ((struct fb_cmap*)argp)->green, registers->len );
			memcpy( registers->blue, ((struct fb_cmap*)argp)->blue, registers->len );*/
			break;
		case FBIOGETCMAP:
			DPRINTF( "FBIOGETCMAP\n" );
/*			((struct fb_cmap*)argp)->start = 0;
			((struct fb_cmap*)argp)->len = registers->len;
			memcpy( ((struct fb_cmap*)argp)->red, registers->red, registers->len );
			memcpy( ((struct fb_cmap*)argp)->green, registers->green, registers->len );
			memcpy( ((struct fb_cmap*)argp)->blue, registers->blue, registers->len );*/
			break;
		case FBIOPAN_DISPLAY:
/*			DPRINTF( "FBIOPAN_DISPLAY" );
			TRY( memcpy( &(fb_var), argp, sizeof(fb_var) ), EFAULT );
DPRINTF(": %dx%dx%d %d%d%d%d\nxres_virtual=%d yres_virtual=%d\nxoffset=%d yoffset=%d\n", fb_var.xres, fb_var.yres, fb_var.bits_per_pixel, fb_var.red.length, fb_var.green.length, fb_var.blue.length,fb_var.transp.length, fb_var.xres_virtual, fb_var.yres_virtual, fb_var.xoffset,fb_var.yoffset );*/
			break;
		case FBIOBLANK:
			DPRINTF( "FAKING ioctl [0x%x]\n", request );
			break;
#if 1
		case FBIOPUT_CON2FBMAP:
		case FBIOGET_CON2FBMAP:
			((struct fb_con2fbmap*) argp)->console = -1;
			((struct fb_con2fbmap*) argp)->framebuffer = -1;
			return 0;
#endif
		default:
			DPRINTF( "unknown ioctl [0x%x]\n", request );
			errno = EFAULT;
			return -1;
	}

	return 0;
}

int close( int fd )
{
	static CLOSE real_close = NULL;

	if( !real_close )
		real_close = (CLOSE) dlsym( RTLD_NEXT, "close" );
		
	if( drvfd != -1 && fd == drvfd ) {
		real_close( drvfd );
		drvfd = -1;
	} else
		return real_close( fd );

	DPRINTF( "close %d\n", fd );

	return 0;
}

// EOF
