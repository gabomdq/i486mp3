#define IDT_VECTORS 0x40
#define TMR_IRQ		0x20
#define MP3_BASE	57344	//lugar en memoria donde esta el mp3
#define MP3_SIZE	19240		//tamaño del mp3
#define MEM_BASE	57344+MP3_SIZE		//comienzo de la memoria dinamica.
#define MAX_STACK	1024
#define MAX_MEM		0x4000 + (128*1024-MEM_BASE) - MAX_STACK //16kb de cache + xx kb de memoria comun - 1k de stack
#define CACHE_BASE	0x20000		//direccion de memoria donde colocar los 16kb de cache

#define NMALLOC_ORG	64
#define CLOCKIRQ	3579545		//[Hz]
#define CLOCK_PER	2793658*3		//Periodo del clock en picosegundos.

struct IDT_DESC
{
	short unsigned offl;
	short unsigned seg;
	short unsigned flags;
	short unsigned offh;
};

struct buffer {
	unsigned char const *start;
	unsigned long length;
};

struct malloc_org {					//Estructura para organizar los mallocs
	unsigned base;			
	unsigned size;			//El bit MSB indica si esta usado o no el bloque.(cero=NO USADO)
};


static IDT_DESC idt[IDT_VECTORS];
static malloc_org morg[NMALLOC_ORG];

static struct {
	unsigned char bytes[6];
} IDT_load;

static union 
{
	unsigned outdw;			//este es el buffer que mandamos a los ports.
	struct {
		unsigned char dac;
		unsigned char tmr;
		unsigned char irqcode;
		unsigned char unused;
	} port;
};

static struct 
{
	bool rdy, wait;
	unsigned samples;
	unsigned div,divinc;
	unsigned char rdbuf, wrbuf;
	unsigned char buf[2][1152];
} out_ctrl;
void out(unsigned val, short unsigned port=0);
void timer(void);

#include "mad\mad.h"

static enum mad_flow input(void *data,struct mad_stream *stream);
static enum mad_flow output(void *data, struct mad_header const *header, struct mad_pcm *pcm);
static enum mad_flow error(void *data, struct mad_stream *stream, struct mad_frame *frame);
unsigned char scale(mad_fixed_t sample);

extern "C"
{
	void *malloc(unsigned);
	void *calloc(unsigned,unsigned);
	void free(void *);
	void reportar (unsigned line);
}