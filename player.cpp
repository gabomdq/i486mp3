//Reproductor MP3 embedded
//Gabriel Jacobo - 2002

/* Mapa de memoria:

  Desde 00000h a 01000h - Archivo comprimido MP3
  Desde 01000h a finprg - Programa + datos estáticos
  Desde finprg a 24000h - Datos dinámicos (datos + stack).
  Desde 20000h a 24000h - Memoria cache (se une con la memoria normal).
  Desde 1FFF0h a 1FFFFh - Bootcode (se lo sobreescribe luego con datos!)
*/

#include "player.h"

#define MYDEBUG

#ifdef MYDEBUG
#define report reportar(__LINE__)
#else
#define report 
#endif

static unsigned *report_p;

main()
{
	
	static struct buffer buffer;
	static struct mad_decoder decoder;

	/*Inicializacion de la plaqueta*/
	report_p=(unsigned *)0;
	report;

	outdw=0;
	port.irqcode=TMR_IRQ;			//Setear codigo de IRQ
	port.tmr=0xfe;
	out(outdw);
	unsigned short *idtsize=(unsigned short *)&IDT_load.bytes[0];
	unsigned *idtbase = (unsigned *) &IDT_load.bytes[2];
	*idtsize=sizeof(IDT_DESC) * IDT_VECTORS - 1;
	*idtbase=(unsigned) idt;

	/*Inicializacion de la tabla de interrupciones*/
	for(unsigned j=0;j<IDT_VECTORS;j++)
	{
		idt[j].offl=0;
		idt[j].offh=0;
		idt[j].flags=0;
		idt[j].seg=0;
	}

	/*Inicializacion de la entrada 20h de la tabla...apunta a nuestra funcion timer*/
	unsigned timer_ptr=(unsigned) timer;
	idt[TMR_IRQ].offl = timer_ptr & 0xffff;
	idt[TMR_IRQ].offh = timer_ptr >> 16;
	idt[TMR_IRQ].seg = 0x08;
	idt[TMR_IRQ].flags = 0x8E00;	//P=1 DPL=00 0 D=1 1 1 0 0 0 0 x x x x x

	/*Cargar la tabla*/
	unsigned char *px3=&IDT_load.bytes[0];
	_asm
	{
		push ebx;
		mov ebx,px3;
		lidt [ebx];
		pop ebx;
	}


	/*Inicialización del cache en la direccion 20000h a 24000h (16kb)*/
	/*Se utilizan los registros de test de cache para programarlo*/
	
	unsigned tag=CACHE_BASE >> 12;

	_asm
	{
		push eax;
		push ebx;
		//Poner a cero el cache fill buffer de 16 bytes
		xor eax,eax;
		mov tr5,eax;		//TR5= Write fill buffer [0]
		mov tr3,eax;		//TR3=0

		mov eax,0x4;
		mov tr5,eax;		//TR5= Write fill buffer [1]
		xor eax,eax;
		mov tr3,eax;		//TR3=0
		
		mov eax,0x8;
		mov tr5,eax;		//TR5= Write fill buffer [2]
		xor eax,eax;
		mov tr3,eax;		//TR3=0
		
		mov eax,0xc;
		mov tr5,eax;		//TR5= Write fill buffer [3]
		xor eax,eax;
		mov tr3,eax;		//TR3=0
	}


	for(unsigned cache_bank=0;cache_bank<4;cache_bank++)
	{
		//Setear los 4 bancos 
		for(unsigned cache_set=0;cache_set<256;cache_set++)
		{
			//Setear los 256 sets de datos.
			_asm
			{
				mov eax,tag;				//Cache Status: TAG + Valid Bit
				shl eax,12;
				or eax, 0x400;				//EAX=TR4
				
				mov ebx,cache_set;			//EBX=TR5
				shl ebx,2;
				or ebx,cache_bank;
				shl ebx,2;
				or ebx,0x1;

				mov tr4,eax;				//Entre estas dos instrucciones no puede haber
				mov tr5,ebx;				//referencias externas a memoria.
			}
		}
		tag++;
	}
	_asm 
	{
		pop ebx;
		pop eax;
	}

	/*Movilizar el stack desde 1ffech a 24000h*/
	report;
	unsigned *stack_old, *stack_new;
	unsigned *stack_pointer;
	
	_asm mov stack_pointer,esp;

	stack_old=(unsigned *) (0x1fff0-4);
	stack_new=(unsigned *) (0x24000-4);		//4 bytes antes del tope

	while(stack_old!=stack_pointer)
	{
		*stack_new=*stack_old;
		stack_new--;
		stack_old--;
	}

	unsigned esp_new=(unsigned)stack_new;
	_asm mov esp,esp_new;

	/*Inicializar el FPU*/
	_asm finit;
	report;


	/*Inicializacion de los bloques para organizar los mallocs*/
	for(j=0;j<NMALLOC_ORG;j++)
	{
		morg[j].base=0;
		morg[j].size=0;
	}
	
	morg[0].size=MAX_MEM;		//el msb indica que esta libre (=cero).


	/*inicializacion de la estructura que se usa para comunicarse con el timer*/
	out_ctrl.rdy=false;
	out_ctrl.wait=false;
	out_ctrl.div=0;
	out_ctrl.divinc=CLOCK_PER*256;		//La división es por 256.
	out_ctrl.rdbuf=0;
	out_ctrl.wrbuf=0;

	for(j=0;j<1152;j++)
	{
		out_ctrl.buf[0][j]=0;
		out_ctrl.buf[1][j]=0;
	}

	/*Comienza la aplicacion*/

	
	int result;
	
	report;
	/* initialize our private message structure */
	
	buffer.start  = (unsigned char *) MP3_BASE;
	buffer.length = MP3_SIZE;
	
	/* configure input, output, and error functions */
	
	mad_decoder_init(&decoder, &buffer,
		input, 0 /* header */, 0 /* filter */, output,
		error, 0 /* message */);
	
	//Iniciar las interrupciones
	report;
	port.tmr=0xff;					//Arranca el contador.
	out(outdw);
	_asm sti;						//Activar interrupciones...y largaron!

	
	/* start decoding */
	report;
	result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
	report;
	reportar(result);
	/* release the decoder */
	
	mad_decoder_finish(&decoder);
	
	reportar(0xaabbaabb);
	_asm hlt;

	return 0;
}


/*
* This is the input callback. The purpose of this callback is to (re)fill
* the stream buffer which is to be decoded. In this example, an entire file
* has been mapped into memory, so we just call mad_stream_buffer() with the
* address and length of the mapping. When this callback is called a second
* time, we are finished decoding.
*/

static
enum mad_flow input(void *data,
					struct mad_stream *stream)
{
	struct buffer *buffer = (struct buffer *) data;
	
	/*if (!buffer->length)
		return MAD_FLOW_STOP;*/		//Reproduccion continua!!!
	report;
	mad_stream_buffer(stream, buffer->start, buffer->length);
	report;
	//buffer->length = 0;
	
	return MAD_FLOW_CONTINUE;
}

/*
* This is the output callback function. It is called after each frame of
* MPEG audio data has been completely decoded. The purpose of this callback
* is to output (or play) the decoded PCM audio.
*/

static
enum mad_flow output(void *data,
					 struct mad_header const *header,
					 struct mad_pcm *pcm)
{
	unsigned int nsamples;
	mad_fixed_t const *audio;
	unsigned char *buf;
	//report;
	//out_ctrl.div=CLOCKIRQ/pcm->samplerate >> 8;			//la division por 256 la da el hardware
	report;
	//reportar (out_ctrl.div);

	switch(pcm->samplerate)
	{
	case 8000:
		out_ctrl.div=125000000;		//[picoseg]
		break;
	case 11025:
		out_ctrl.div=90702948;		//[picoseg]
		break;
	case 22050:
		out_ctrl.div=45351474;		//[picoseg]
		break;
	case 44100:
		out_ctrl.div=22675737;
		break;
	case 48000:
		out_ctrl.div=20833333;
		break;
	default:
		out_ctrl.div=125000000;
		break;
	}
	
	nsamples = out_ctrl.samples  = pcm->length;
	
	audio   = pcm->samples[0];
	buf = out_ctrl.buf[out_ctrl.wrbuf];
	
	while (nsamples--) 	*buf++ = scale(*audio++);

	//Esperar que salgan todos los samples anteriores
	out_ctrl.rdy=true;
    
	if(out_ctrl.wrbuf) out_ctrl.wrbuf=0;        //Cambiar de buffer
	else out_ctrl.wrbuf=1;

	while(out_ctrl.wait) _asm nop;
	
	out_ctrl.wait=true;
	
	//report;
	return MAD_FLOW_CONTINUE;
}

/*
* This is the error callback function. It is called whenever a decoding
* error occurs. The error is indicated by stream->error; the list of
* possible MAD_ERROR_* errors can be found in the mad.h (or
* libmad/stream.h) header file.
*/

static
enum mad_flow error(void *data,
					struct mad_stream *stream,
					struct mad_frame *frame)
{
	struct buffer *buffer = (struct buffer *)data;

  /*fprintf(stderr, "decoding error 0x%04x (%s) at byte offset %u\n",
  stream->error, mad_stream_errorstr(stream),
	stream->this_frame - buffer->start);*/
	report;
	reportar(stream->error);
	reportar(stream->this_frame - buffer->start);
	_asm hlt;
	return MAD_FLOW_BREAK;
}


void *malloc(unsigned size)
{
	//Entrega segmentos mapeados en la memoria cache.
	report;
	reportar(size);
	if(!size) return 0;

	for(unsigned j=0;j<NMALLOC_ORG;j++)
	{
		//Buscar un bloque libre, igual o mas grande al necesario.
		if(!(morg[j].size & 0x80000000) && morg[j].size>=size)
		{
			//Bloque no usado y del tamaño adecuado.
			for(unsigned k=0;k<NMALLOC_ORG;k++)
			{
				//Buscar un bloque libre, de tamaño cero.
				if(!morg[k].size) break;
			}
			if(k==NMALLOC_ORG) _asm hlt;	//No hay mas bloques libres.
			
			morg[k].base=morg[j].base;
			morg[k].size=0x80000000 | size;
			morg[j].base+=size;
			morg[j].size-=size;

			//Poner el bloque todo a cero.
			unsigned *ptr=(unsigned *) ( ( (unsigned) morg[k].base ) + (unsigned) MEM_BASE);
			unsigned *ptr1=ptr;

			for(k=0;k<(size>>2);k++)
			{
				*ptr1=0;		//Se hace de a 32 bits.
				ptr1++;			//Si hay bytes mas alla de un multiplo de 4 no se borran.
			}

			report;
			return (void *) ptr;
		}
	}


	if(j==NMALLOC_ORG) _asm hlt;


	return 0;
}

void free(void *ptr)
{
	report;
	if((unsigned)ptr<MEM_BASE) return;
	short unsigned base = (unsigned)ptr - (unsigned) MEM_BASE;

	for(unsigned j=0;j<NMALLOC_ORG;j++)
	{
		//Buscar el bloque correspondiente.
		if(morg[j].base==base && (morg[j].size & 0x80000000) )
		{
			morg[j].size&=0x4FFF;		//Borrar el MSB, queda marcado como no usado.
		}
	}

	if(j==NMALLOC_ORG) _asm hlt;	//Bloque no encontrado.

	//Desfragmentar los bloques.

	for(j=0;j<NMALLOC_ORG;j++)
	{
		if(morg[j].size & !(morg[j].size & 0x80000000) )
		{
			//Bloque de tamaño > cero y no usado.

			//Buscar si hay otro bloque mas.

			for(unsigned k=j+1;k<NMALLOC_ORG;k++)
			{
				if(morg[k].size & !(morg[k].size & 0x80000000) )
				{
					//Hay otro bloque mas! ...ver si los podemos unir...
					if(morg[j].base+morg[j].size==morg[k].base
						|| morg[k].base+morg[k].size==morg[j].base)
					{
						//los bloques son consecutivos...unirlos.
						//Si la base de j es mayor que la de k, cambiarla...sino dejarla tal cual.
						if(morg[j].base>morg[k].base) morg[j].base=morg[k].base;
						morg[j].size+=morg[k].size;
						morg[k].size=0;	//Borrar este bloque.
						morg[k].base=0;
						j=0;			//Obligar a recomenzar el proceso.
						break;
					}
				}
			}
		}
	}
	report;

}

void *calloc(unsigned nitems, unsigned size)
{
	//Entrega segmentos mapeados en la memoria cache.
	report;
	return malloc(nitems*size);
}


void out(unsigned val, short unsigned port )
{
	_asm
	{
		push eax;
		push dx;
		mov eax,val;
		mov dx,port;
		out dx,eax;
		pop dx;
		pop eax;
	}
}

__declspec ( naked ) void timer(void)
{
	//_asm cli;
	_asm pushad;

	static unsigned sample=0, div=0;

	/*Codigo*/
	if(out_ctrl.rdy)
	{
		if(div>out_ctrl.div)
		{
			div-=out_ctrl.div;

			port.dac=out_ctrl.buf[out_ctrl.rdbuf][sample];
			out(outdw);
			sample++;

			if(sample>=out_ctrl.samples)
			{
				//Se acabaron los samples.
				sample=0;
				if(out_ctrl.rdbuf) out_ctrl.rdbuf=0;		//Cambiar de buffer
				else out_ctrl.rdbuf=1;

				out_ctrl.wait=false;
			}
		}
		else div+=out_ctrl.divinc;
	}
	
	//Preparar para una nueva interrupcion.
	port.tmr=0xfe;
	out(outdw);
	port.tmr=0xff;
	out(outdw);
	
	/*Fin Codigo*/
	_asm
	{
		popad;
		//sti;
		iretd;
	}
}

void reportar (unsigned line)
{
	*report_p=line;
	report_p++;
	if((unsigned)report_p>=0x100) report_p=0;
}


unsigned char scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 8));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return (sample >> (MAD_F_FRACBITS + 1 - 8))+127;
}
