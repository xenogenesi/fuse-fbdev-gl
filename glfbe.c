//To compile $ gcc -O NAME.c -o OUTPUTNAME -lglut

#include <GL/glut.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "fbemem.h"

#define win_w 720
#define win_h 576

static struct priv_registers priv = {
	.id = "glfbemu",
	.msize = MEMSIZE,
	.res = RES_PAL,
	.depth = DEP_24,
	.status = ST_INIT,
};

static unsigned char* videobuff;
static struct priv_registers* registers = &priv;

void init(void)
{
	glShadeModel(GL_FLAT);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	glDisable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT);
}

void reshape(int w, int h)
{
	glViewport(0, 0, (GLsizei) w, (GLsizei) h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0.0, (GLdouble) w, 0.0, (GLdouble) h);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void display(void)
{
	unsigned int xres = 720, yres = 576;

	switch(registers->res)
	{
		case RES_PAL:
			xres = 720; yres = 576;
			break;
		case RES_640x480:
			xres = 640; yres = 480;
			break;
		default:
			registers->status = ST_ERROR;
	}

	glRasterPos2i(0, yres - 1); // vertical flip
	glPixelZoom(1.0f, -1.0f);

	switch(registers->depth)
	{
		case DEP_32:
			glDrawPixels(xres, yres, GL_BGRA, GL_UNSIGNED_BYTE, (GLubyte*) videobuff);
			break;
		case DEP_24:
			glDrawPixels(xres, yres, GL_BGR, GL_UNSIGNED_BYTE, (GLubyte*) videobuff);
			break;
		case DEP_16:
			glDrawPixels(xres, yres, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (GLubyte*) videobuff);
			break;
		case DEP_8:
			//
			break;
		default:
			printf("Unsupported mode: %dbpp\n", registers->depth);
			registers->status = ST_ERROR;
	}
	
	glFlush();
}

void keyboard(unsigned char key, int x, int y)
{
	switch(key)
	{
		case 27:
			exit(0);
			break;
		default:
			break;
	}
}

void timer(int value)
{
	glutPostRedisplay();
	glutTimerFunc(33, timer, 1);
}

int main(int argc, char* argv[])
{
	int fdfb;

	printf("OpenGL Framebuffer Emulator\n");
	printf("Memsize %d/kB\n", MEMSIZE/1024);

	fdfb = open(getenv("FRAMEBUFFER"), O_CREAT|O_WRONLY, 0777);

	ftruncate(fdfb, MEMSIZE);
	lseek(fdfb, 0, SEEK_END);
	write(fdfb, registers, sizeof( struct priv_registers ));
	close(fdfb);

	fdfb = open(getenv("FRAMEBUFFER"), O_RDWR);

	if( !(videobuff = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fdfb, 0)))
		exit(1);

	if( !(registers = mmap(NULL, sizeof( struct priv_registers ), PROT_READ|PROT_WRITE, MAP_SHARED, fdfb, MEMSIZE)))
		exit(1);

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
	glutInitWindowSize(win_w, win_h);
	glutInitWindowPosition(100, 100);
	glutCreateWindow("OpenGL Framebuffer Emulator");

	init();
	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutTimerFunc(33, timer, 1);

	glutMainLoop();
	
	return 0;
}
