#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include "mp3player.h"

#include "mem2.h"
#include "globals.h"
#include "mystring.h"

#define MAXITEMS 192
#define PRIO 64
#define TSIZE 192.0
#define ELEMENT ((int)TSIZE * (int)TSIZE * 4)
#define BLOCK (ELEMENT * MAXITEMS)

static mutex_t mutex_process;
static mutex_t mutex_master;

#define STACKSIZE	8192
static u8 * cache = NULL;
static u8 * threadStack = NULL;
static lwp_t hthread = LWP_THREAD_NULL;

typedef struct 
	{
	char id[256];
	GRRLIB_texImg *cover;
	u8 prio;	// 1 or 0, 
	u32 age;
	}
s_cc; // covercache

static s_cc *cc = NULL;

static int age = 0;
static volatile bool threadStop = false;
static volatile bool threadRunning = false;
static volatile int cycle = 0;
static volatile int cId = 0;
static volatile int update = 0;
static volatile int doPrio = 0;

GRRLIB_texImg*  CoverCache_LoadTextureFromFile(char *filename);

static GRRLIB_texImg *MoveTex2Mem2 (GRRLIB_texImg *tex, int index)
	{
	if (!tex || !tex->data) return NULL;
	
	u32 size = tex->w * tex->h * 4;
	
	if (size == 0 || size > ELEMENT)
		{
		GRRLIB_FreeTexture (tex);
		return NULL;
		}
		
	u8* data = &cache[index * ELEMENT];
	memcpy (data, tex->data, size);

	free (tex->data);
	tex->data = data;
	
	GRRLIB_FlushTex (tex);
	
	return tex;
	}
	
static void FreeMem2Tex (GRRLIB_texImg *tex)
	{
	DCFlushRange(tex, sizeof(GRRLIB_texImg));
	if (!tex) return;
	free(tex);
	}

static void *thread (void *arg)
	{
	GRRLIB_texImg *tex;
	char name[256];

	threadRunning = 1;

	int i;
	int read;
	
	while (true)
		{
		read = 0;
		
		cycle++;
		
		for (i = 0; i < MAXITEMS; i++)
			{
			LWP_MutexLock (mutex_master);
			
			if (*cc[i].id != '\0' && !cc[i].cover && ((doPrio == 1 && cc[i].prio == 1) || doPrio == 0))
				{
				LWP_MutexLock (mutex_process);
				read = 1;
				cId = i;
				strcpy (name, cc[i].id);
				LWP_MutexUnlock (mutex_process);
				
				LWP_MutexUnlock (mutex_master);
				tex = CoverCache_LoadTextureFromFile (name);
				LWP_MutexLock (mutex_master);

				LWP_MutexLock (mutex_process);
				cc[i].prio = 0;
 				cc[i].cover = MoveTex2Mem2 (tex, i);
				
				if (!cc[i].cover) 
					{
					*cc[i].id = '\0'; // do not try again
					}
				DCFlushRange(&cc[i], sizeof(s_cc));
				update ++;
				LWP_MutexUnlock (mutex_process);

				cId = -1;
				
				usleep (10);
				}
			
			LWP_MutexUnlock (mutex_master);
				
			if (threadStop)
				{
				threadRunning = 0;
				return NULL;
				}
			}
			
		if (read == 0) 
			{
			usleep (1000);
			}

		doPrio = 0;
		}
	
	return NULL;
	}

void CoverCache_Lock (void) // return after putting thread in 
	{
	LWP_MutexLock (mutex_master);
	}
	
void CoverCache_Unlock (void) // return after putting thread in 
	{
	doPrio = 1;
	LWP_MutexUnlock (mutex_master);
	}
	
void CoverCache_Start (void)
	{
	Debug ("CoverCache_Start");
	
	cc = calloc (1, MAXITEMS * sizeof(s_cc));
	cache = mem2_malloc (BLOCK);
	
	Debug ("cache = 0x%X (size = %u Kb)", cache, BLOCK);

	mutex_master = mutex_process = LWP_MUTEX_NULL;
    LWP_MutexInit (&mutex_process, false);
	LWP_MutexInit (&mutex_master, false);

	LWP_CreateThread (&hthread, thread, NULL, NULL, 0, PRIO);
	LWP_ResumeThread(hthread);
	}

void CoverCache_Stop (void)
	{
	if (!cc) return; // start wasn't called
	
	Debug ("CoverCache_Stop");
	//monrunning = false;
	if (threadRunning)
		{
		threadStop = 1;
		while (threadRunning) usleep (1000);
		LWP_JoinThread (hthread, NULL);
		CoverCache_Flush ();
		free (threadStack);
		free (cc);
		mem2_free (cache);
		LWP_MutexDestroy (mutex_process);
		LWP_MutexDestroy (mutex_master);
		}
	else
		Debug ("CoverCache_Stop: thread was already stopped");
	}
	
void CoverCache_Flush (void)	// empty the cache
	{
	if (!cc) return; // start wasn't called
	
	int i;
	int count = 0;
	Debug ("CoverCache_Flush");
	
	if (threadRunning) CoverCache_Lock ();
	
	DCFlushRange(cc, MAXITEMS * sizeof(s_cc));
	
	for (i = 0; i < MAXITEMS; i++)
		{
		if (cc[i].cover) 
			{
			FreeMem2Tex (cc[i].cover);
			cc[i].cover = NULL;
			
			count ++;
			}
			
		*cc[i].id = '\0';
		cc[i].age = 0;
		}
	age = 0;
	
	DCFlushRange(cc, MAXITEMS * sizeof(s_cc));

	if (threadRunning) CoverCache_Unlock ();

	Debug ("CoverCache_Flush: %d covers flushed", count);
	}
	
GRRLIB_texImg *CoverCache_Get (char *id) // this will return the same text
	{
	if (threadRunning == 0) return NULL;

	int i;
	GRRLIB_texImg *tex = NULL;
	
	LWP_MutexLock (mutex_process);
	
	for (i = 0; i < MAXITEMS; i++)
		{
		if (i != cId && *cc[i].id != '\0' && strcmp (id, cc[i].id) == 0)
			{
			tex = cc[i].cover;
			break;
			}
		}
	
	LWP_MutexUnlock (mutex_process);

	return tex;
	}
	
GRRLIB_texImg *CoverCache_GetCopy (char *id) // this will return a COPY of the required texture
	{
	return grlib_CreateTexFromTex (CoverCache_Get (id));
	}
	
bool CoverCache_IsUpdated (void) // this will return the same text
	{
	static int lastCheck = 0;
	static u32 mstout = 0;
	
	u32 ms = ticks_to_millisecs(gettime());
	
	if (mstout < ms)
		{
		mstout = ms + 100;
		
		if (lastCheck != update)
			{
			lastCheck = update;
			return true;
			}
		}
	return false;
	}

// CoverCache_Lock must be invocked before calling this, otherwhise unpredictable result may happens
void CoverCache_Add (char *id, bool priority)
	{
	int i;
	bool found = false;
	
	DCFlushRange(cc, MAXITEMS * sizeof(s_cc));
	
	// Let's check if the item already exists...
	for (i = 0; i < MAXITEMS; i++)
		{
		if (*cc[i].id != '\0' && strcmp (id, cc[i].id) == 0) 
			{
			return;
			}
		}
	
	// first search blank...
	for (i = 0; i < MAXITEMS; i++)
		{
		if (*cc[i].id == '\0')
			{
			strcpy (cc[i].id, id);
			cc[i].age = age;
			cc[i].prio = priority;
			found = true;
			break;
			}
		}
		
	// If we haven't found the item search for the oldest one
	if (!found)
		{
		u32 minage = 0xFFFFFFFF;
		int minageidx = 0;
		
		for (i = 0; i < MAXITEMS; i++)
			{
			if (&cc[i].id != '\0' && cc[i].age < minage)
				{
				minage = cc[i].age;
				minageidx = i;
				}
			}
		
		int idx = minageidx;
		
		if (cc[idx].cover) 
			{
			FreeMem2Tex (cc[idx].cover);
			cc[idx].cover = NULL;
			}
			
		strcpy (cc[idx].id, id);
		cc[idx].age = age;
		cc[idx].prio = priority;
		}

	DCFlushRange(cc, MAXITEMS * sizeof(s_cc));
	age ++;
	}

/*

*/
u8 * LoadRGBTexRGBA (char *fn, u16* w, u16* h, u8 alpha)
	{
	u16 *wh;

	u32 x, y;

	size_t size_rgb;
	u8* rgb;
	u8* prgb;

	u32 size_rgba;
	u8* rgba;
	
	*w = 0;
	*h = 0;
	
	//Debug ("Loading %s", fn);
	
	rgb = fsop_ReadFile (fn, 0, &size_rgb);
	if (!rgb)
		{
		//Debug ("...not found", fn);
		return NULL;
		}
	
	wh = (u16*)(rgb + size_rgb - 4);
	
	//Debug ("size = %d, %d, %d, %u", size_rgb, wh[0], wh[1], (void*)wh-(void*)rgb);
	
	if (wh[0] == 0 || wh[1] == 0 || wh[0] > TSIZE || wh[1] > TSIZE)
		{
		free (rgb);
		return NULL;
		}
	
	size_rgb = wh[0]*wh[1]*3;
	size_rgba = wh[0]*wh[1]*4;
		
	rgba = malloc (size_rgba);
	if (!rgba)
		{
		free (rgb);
		return NULL;
		}
	
	prgb = rgb;
	for (x = 0; x < wh[0]; x++)
		for (y = 0; y < wh[1]; y++)
			{
			GRRLIB_SetPixelTotexImg4x4RGBA8 (x, y, wh[0], rgba, RGBA (prgb[0], prgb[1], prgb[2], alpha));
			prgb += 3;
			}
	
		
	free (rgb);
	
	*w = wh[0];
	*h = wh[1];
	return rgba; 
	}

bool SaveRGBATexRGB (char *fn, u8* rgba, u16 w, u16 h)
	{
	FILE *f;
	u16 wh[2];
	u32 x,y;
	u32 size_rgb;
	u8* rgb;
	u8* prgb;
	u32 pixel;
	
	size_rgb = w*h*3;

	rgb = malloc (size_rgb);
	if (!rgb)
		{
		return false;
		}

	prgb = rgb;
	for (x = 0; x < w; x++)
		for (y = 0; y < h; y++)
			{
			pixel = GRRLIB_GetPixelFromtexImg4x4RGBA8 (x, y, w, rgba);
			*(prgb++) = R(pixel);
			*(prgb++) = G(pixel);
			*(prgb++) = B(pixel);
			}
	
	f = fopen (fn, "wb");
	if (!f) 
		{
		//Debug ("SaveRGBATexRGB: cannot write to '%s'", fn);
		free (rgb);
		return false;
		}
	
	fwrite (rgb, 1, size_rgb, f);
	wh[0] = w;
	wh[1] = h;
	fwrite (wh, 1, sizeof(wh), f);

	fclose (f);
		
	free (rgb);
	return true; 
	}

u8 * LoadRGBATex (char *fn, u16* w, u16* h, u8 alpha)
	{
	u32 size_rgba;
	u8* rgba;
	u16 *wh;
	
	*w = 0;
	*h = 0;
	
	rgba = fsop_ReadFile (fn, 0, &size_rgba);
	if (!rgba)
		{
		//Debug ("'%s' NOT loaded ...", fn);
		return NULL;
		}
	
	wh = (u16*)(rgba + size_rgba - 4);

	*w = wh[0];
	*h = wh[1];
	
	//Debug ("'%s' loaded succesfully...", fn);

	return rgba; 
	}

bool SaveRGBATex (char *fn, u8* rgba, u16 w, u16 h)
	{
	FILE *f;
	u16 wh[2];
	u32 size_rgba;
	
	if (w == 0 || h == 0 || !rgba) return false;
	
	size_rgba = w*h*4;

	f = fopen (fn, "wb");
	if (!f) 
		{
		Debug ("SaveRGBATexRGB: cannot write to '%s'", fn);
		return false;
		}
	
	wh[0] = w;
	wh[1] = h;
	fwrite (rgba, 1, size_rgba, f);
	fwrite (wh, 1, sizeof(wh), f);
	fclose (f);
	
	return true; 
	}

GRRLIB_texImg* CoverCache_LoadTextureFromFile(char *filename) 
	{
    GRRLIB_texImg  *tex = NULL;
    unsigned char  *data;
	char fntex[256];
	char buff[256];
	u8 *rgba;
	u16 i, j, w, h;
	float fw, fh;
	u32 t1 = get_msec(0);
	
	//Debug ("CoverCache_LoadTextureFromFile: %s", filename);
	
	// let's clean the filename
	strcpy (fntex, filename);

	// kill extension
	j = strlen (fntex);
	while (j > 0 && fntex[j--] != '.');
	fntex[j+1] = 0;
	
	j = 0;
	for (i = 0; i < strlen(fntex); i++)
		{
		if ((fntex[i] >= 48 && fntex[i] <= 57) || (fntex[i] >= 65 && fntex[i] <= 90) || (fntex[i] >= 97 && fntex[i] <= 122))
			{
			buff[j++] = fntex[i];
			}
		}
	buff[j] = 0;
	ms_strtolower(buff);
	
	// let's take max 16 char for texname
	if (j < 16)
		{
		sprintf (fntex, "%s://ploader/tex/%s.tex", vars.defMount, buff);
		}
	else
		{
		sprintf (fntex, "%s://ploader/tex/%s.tex", vars.defMount, &buff[j-16]);
		}
	
	//Debug ("fntex = %s", fntex);
		
	//mt_Lock ();
	rgba = LoadRGBATex (fntex, &w, &h, 255);
	//mt_Unlock ();
	
	Debug ("CoverCache_LoadTextureFromFile->step1 took %u msec", get_msec(0) - t1);
	
	if (!rgba)
		{
		size_t size;
		// return NULL it load fails
		//mt_Lock ();
		u32 t2 = get_msec(0);
		data = fsop_ReadFile (filename, 0, &size);
		Debug ("CoverCache_LoadTextureFromFile->fsop_ReadFile %u msec (%u)", get_msec(0) - t2, size);
		//mt_Unlock ();
		if (!data)
			{
			free (rgba);
			goto quit;
			}
		
		// Convert to texture
		tex = GRRLIB_LoadTexture(data);
		Debug ("CoverCache_LoadTextureFromFile->GRRLIB_LoadTexture %u msec", get_msec(0) - t2);
		
		// Free up the buffer
		free(data);
		
		if (!tex) 
			{
			free (data);
			free (rgba);
			goto quit;
			}
		
		if (tex->w >= tex->h && tex->w > TSIZE)
			{
			fw = TSIZE;
			fh = TSIZE / ((float)tex->w / (float)tex->h);;
			}
		else if (tex->w < tex->h && tex->h > TSIZE)
			{
			fw = TSIZE / ((float)tex->h / (float)tex->w);
			fh = TSIZE;
			}
		else
			{
			fw = tex->w;
			fh = tex->h;
			}
			
		w = (u16)fw / 4 * 4;
		h = (u16)fh / 4 * 4;
			
		rgba = malloc (w * h *4);
		ResizeRGBA (tex->data, tex->w, tex->h, rgba, w, h);
		GRRLIB_FreeTexture (tex);
		Debug ("CoverCache_LoadTextureFromFile->ResizeRGBA %u msec", get_msec(0) - t2);

		if (config.enableTexCache)
			{
			//mt_Lock ();
			//LWP_SetThreadPriority (hthread, LWP_PRIO_HIGHEST);
			SaveRGBATex (fntex, rgba, w, h);
			//LWP_SetThreadPriority (hthread, PRIO);
			//mt_Unlock ();
			}
			
		Debug ("CoverCache_LoadTextureFromFile->SaveRGBATex %u msec (%d)", get_msec(0) - t2, config.enableTexCache);
		}

	tex = calloc(1, sizeof(GRRLIB_texImg));
	tex->w = w;
	tex->h = h;
	tex->data = rgba;
	GRRLIB_SetHandle( tex, 0, 0 );
	GRRLIB_FlushTex( tex );
	
quit:
	Debug ("CoverCache_LoadTextureFromFile->took %u msec", get_msec(0) - t1);
    return tex;
	}
