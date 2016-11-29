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
		fprintf( stderr, "[%d]:%s: ", getpid(), __FILE__ ); \
		fprintf( stderr, format, ## args ); \
	} while( 0 )

#else
#define DPRINTF( format, args... )
#endif

#define TRY( c, e ) { if( !(c) ) { errno = e; return -1; } }

//static unsigned char* videobuff;

typedef int request_t;

static int drvfd = -1;
static int tty_to_watch = -1;
static int tty_fd = -1;
static int tty0 = -1;

typedef int (* OPEN) ( const char *, int, mode_t );
typedef int (* IOCTL) ( int, request_t, void * );
typedef int (* CLOSE) ( int );

struct st_ioctls {
	unsigned short code;
	const char *name;
};

static unsigned short plain_map[256] = {
	0xf200,	0xf01b,	0xf031,	0xf032,	0xf033,	0xf034,	0xf035,	0xf036,
	0xf037,	0xf038,	0xf039,	0xf030,	0xf02d,	0xf03d,	0xf07f,	0xf009,
	0xfb71,	0xfb77,	0xfb65,	0xfb72,	0xfb74,	0xfb79,	0xfb75,	0xfb69,
	0xfb6f,	0xfb70,	0xf05b,	0xf05d,	0xf201,	0xf702,	0xfb61,	0xfb73,
	0xfb64,	0xfb66,	0xfb67,	0xfb68,	0xfb6a,	0xfb6b,	0xfb6c,	0xf03b,
	0xf027,	0xf060,	0xf700,	0xf05c,	0xfb7a,	0xfb78,	0xfb63,	0xfb76,
	0xfb62,	0xfb6e,	0xfb6d,	0xf02c,	0xf02e,	0xf02f,	0xf700,	0xf30c,
	0xf703,	0xf020,	0xf207,	0xf100,	0xf101,	0xf102,	0xf103,	0xf104,
	0xf105,	0xf106,	0xf107,	0xf108,	0xf109,	0xf208,	0xf209,	0xf307,
	0xf308,	0xf309,	0xf30b,	0xf304,	0xf305,	0xf306,	0xf30a,	0xf301,
	0xf302,	0xf303,	0xf300,	0xf310,	0xf206,	0xf200,	0xf03c,	0xf10a,
	0xf10b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf30e,	0xf702,	0xf30d,	0xf01c,	0xf701,	0xf205,	0xf114,	0xf603,
	0xf118,	0xf601,	0xf602,	0xf117,	0xf600,	0xf119,	0xf115,	0xf116,
	0xf11a,	0xf10c,	0xf10d,	0xf11b,	0xf11c,	0xf110,	0xf311,	0xf11d,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

static struct st_ioctls ioctls[] = {
	{ 0x4b46, "KDGKBENT" },
	{ 0x4600, "FBIOGET_VSCREENINFO" },
	{ 0x4601, "FBIOPUT_VSCREENINFO" },
	{ 0x4602, "FBIOGET_FSCREENINFO" },
	{ 0x4611, "FBIOBLANK" },
	{ 0x5600, "VT_OPENQRY" },
	{ 0x5601, "VT_GETMODE" },
	{ 0x5602, "VT_SETMODE" },
	{ 0x5603, "VT_GETSTATE" },
	{ 0x5606, "VT_ACTIVATE" },
	{ 0x5607, "VT_WAITACTIVE" },
	{ 0x5608, "VT_DISALLOCATE" },
	{ 0x4b3a, "KDSETMODE" },
	{ 0x4b32, "KDSETLED" },
	{ 0x4b44, "KDGKBMODE" },
	{ 0x4b45, "KDSKBMODE" },
	{ 0, "UNKNOWN" }
};

static const char *decode_ioctl( unsigned short code )
{
	struct st_ioctls *p = ioctls;

	while( p->code != 0 ) {
		if( p->code == code )
			break;
		p++;
	}
	
	return p->name;
}

static int shopen( const char *pathname, int flags, mode_t mode, OPEN nextopen )
{
	int ret = -1;

/*	if( !strcmp( pathname, getenv( "FRAMEBUFFER" ) ) ) {
		ret = drvfd = nextopen( pathname, flags, mode );
	} else*/ if( !strcmp( pathname, "/dev/tty0") )
		ret = tty0 = nextopen( pathname, flags, mode );
	else if( (!strncmp( pathname, "/dev/tty", 8 ) ) ) {
		DPRINTF( "FAKING OPEN TO: %s\n", pathname );
		ret = tty_fd = nextopen( "/dev/tty9", flags, mode );
	} else
		ret = nextopen( pathname, flags, mode );

	return ret;
}

int open( const char *pathname, int flags, ... )
{
	static OPEN real_open = NULL;
	va_list args;
	mode_t mode;

	DPRINTF( "open %s\n", pathname );

	if( !real_open )
		real_open = (OPEN) dlsym( RTLD_NEXT, "open" );

	va_start( args, flags );
	mode = va_arg( args, mode_t );
	va_end( args );

	return shopen( pathname, flags, mode, real_open );
}

int open64( const char *pathname, int flags, ... )
{
	static OPEN real_open64 = NULL;
	va_list args;
	mode_t mode;

	DPRINTF( "open64 %s\n", pathname );

	if( !real_open64 )
		real_open64 = (OPEN) dlsym( RTLD_NEXT, "open64" );

	va_start( args, flags );
	mode = va_arg( args, mode_t );
	va_end( args );

	return shopen( pathname, flags, mode, real_open64 );
}

int ioctl( int fd, request_t request, ... )
{
	static IOCTL real_ioctl = NULL;
	va_list args;
	void *argp;
	int ret;

	if( !real_ioctl )
		real_ioctl = (IOCTL) dlsym( RTLD_NEXT, "ioctl" );

	va_start( args, request );
	argp = va_arg( args, void * );
	va_end( args );

//return real_ioctl( fd, request, argp );

//	if( fd == drvfd )
//		return real_ioctl( fd, request, argp );
//
//	{
//	static int last_req = -1, count = 0;
//	if( request != last_req ) {
//		if( count > 1 )
//		DPRINTF("last ioctl repeated %d\n", count );
//		DPRINTF("ioctl %x (%s) on %d\n", request, decode_ioctl(request), fd );
//		last_req = request;
//		count = 0;
//	} else 
//		count++;
//	}
/*
	if( (fd == tty0) ) {
		//if( request == VT_OPENQRY ) {
			return real_ioctl( fd, request, argp );
			//tty_to_watch = *((int*) argp);
			//DPRINTF( "next tty to watch %d\n", tty_to_watch );
			//return ret;
		//} else if( request == VT_DISALLOCATE ) {
		//	DPRINTF( "match tty %d ignored\n", tty0 );
		//	return 0;
		//}
	} else */
//return real_ioctl( fd, request, argp );
	if( fd == tty_fd ) {
		switch( request )
		{
			case VT_SETMODE:
			case VT_GETMODE: {
				struct vt_mode tmp;
				tmp.mode = KD_GRAPHICS;
				TRY( memcpy(argp, &(tmp), sizeof(tmp) ), EFAULT );
				return 0;
				}			
			//case KDGKBMODE:
				//DPRINTF("GET_MODE: %d\n", argp);
				//argp = 0;
				//return 0;
			case KDSKBMODE:
				DPRINTF("SET_MODE: %p\n", argp);
				switch( (int) argp )
				{
					case K_MEDIUMRAW:
						return real_ioctl( fd, KDGKBMODE, K_MEDIUMRAW );
					default:
						break;
				}
				return 0;
			case KDSETMODE:
			case KDGETMODE:
				*((unsigned char *)argp) = KD_GRAPHICS;
				return 0;
#if 0
			case KDGKBENT:
				DPRINTF("table=%d index=%d value=%d value=%u\n", ((struct kbentry*)argp)->kb_table, ((struct kbentry*)argp)->kb_index, ((struct kbentry*)argp)->kb_value, plain_map[((struct kbentry*)argp)->kb_index] ^ 0xf000 );
				switch( ((struct kbentry*)argp)->kb_table )
				{
					case 0:
						((struct kbentry*)argp)->kb_value = plain_map[((struct kbentry*)argp)->kb_index] ^ 0xf000;
						break;
					default:
						break;
						//return EFAULT;
				}
				//{
					//int i = 0;
					//for(i = 0; i < 256; i++)
					//	printf("%d=%d\n", i, plain_map[i]);
				//}
				return 0;
			case KDSKBLED:
				return 0;
#if 0
			//case VT_GETMODE:
			//case VT_GETSTATE:
			//case KDGETMODE:
			//case VT_DISALLOCATE:
			case KDSETLED:
			case VT_ACTIVATE:
			case VT_WAITACTIVE:
			//case VT_SETMODE:
			//case KDSETMODE:
			case KDGKBMODE:
			case KDSKBMODE:
	//		decode_ioctl(request);
	//ret = real_ioctl( fd, request, argp );
	//break;
	//			DPRINTF( "match tty %d ignored\n", tty_fd );
	//			return 0;
#endif
#endif
			default:
				DPRINTF("IOCTL PASSTHRU: 0x%x (%s)\n", request, decode_ioctl(request));
				break;
		}
	}

	return real_ioctl( fd, request, argp );
}

int close( int fd )
{
	static CLOSE real_close = NULL;

	if( !real_close )
		real_close = (CLOSE) dlsym( RTLD_NEXT, "close" );

	//DPRINTF( "close %d\n", fd );
	
	if( fd == -1 ) {
		errno = EBADF;
		return -1;
	}

	if( fd == drvfd )
		drvfd = -1;
	else if( fd == tty0 )
		tty0 = -1;
	else if( (tty_to_watch != -1) && (tty_fd != -1 ) && (fd == tty_fd) ) {
		tty_fd = -1;
		return 0;
	}

	return real_close( fd );
}

