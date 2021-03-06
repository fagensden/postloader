/*////////////////////////////////////////////////////////////////////////////////////////

fsop contains coomprensive set of function for file and folder handling

en exposed s_fsop fsop structure can be used by callback to update operation status

(c) 2013 stfour

////////////////////////////////////////////////////////////////////////////////////////*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <ogcsys.h>
#include <ogc/lwp_watchdog.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h> //for mkdir 
#include <sys/statvfs.h>

#include "fsop.h"
#include "../debug.h"

#define SET(a, b) a = b;

s_fsop fsop;

// return a separated char array with directory items. '.' and '..' will be skipped. Terminated with \0, as usual. ext ".bmp;.png;.piripip�'"
char * fsop_GetDirAsString (char *path, char sep, int skipfolders, char *ext)
	{
	DIR *pdir;
	struct dirent *pent;
	char *buff,*p;
	int size = 0;

	pdir = opendir (path);
	if (!pdir) return NULL;
	
	while ((pent=readdir(pdir)) != NULL) 
		{			
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0 || (pent->d_type == DT_DIR && skipfolders)) continue;
		
		if (ext)
			{
			char *e = fsop_GetExtension(pent->d_name);
			if (e && !strstr (ext, e)) continue; 
			}
		
		size += (strlen (pent->d_name)+1);
		}
	
	size++;
	
	buff = p = calloc (1, size);
	if (!buff) return NULL;
	
	rewinddir (pdir);
	while ((pent=readdir(pdir)) != NULL) 
		{			
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0 || (pent->d_type == DT_DIR && skipfolders)) continue;

		if (ext)
			{
			char *e = fsop_GetExtension(pent->d_name);
			if (e && !strstr (ext, e)) continue; 
			}
		
		strcpy (p, pent->d_name);
		p += strlen (pent->d_name);
		*p = sep;
		p += 1;
		}
	
	closedir (pdir);
	return buff;
	}
	
// return a separated char array with directory items followed by type (ascii char). '.' and '..' will be skipped. Terminated with \0, as usual. ext ".bmp;0;.png;0;.piripip�;1"
char * fsop_GetDirAsStringWithDirFlag (char *path, char sep)
	{
	DIR *pdir;
	struct dirent *pent;
	char *buff,*p;
	int size = 0;

	pdir = opendir (path);
	if (!pdir) return NULL;
	
	while ((pent=readdir(pdir)) != NULL) 
		{			
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0) continue;
		size += (strlen (pent->d_name)+3);
		}
	
	size++;
	
	buff = p = calloc (1, size);
	if (!buff) return NULL;
	
	rewinddir (pdir);
	while ((pent=readdir(pdir)) != NULL) 
		{			
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0) continue;

		strcpy (p, pent->d_name);
		p += strlen (pent->d_name);
		*p = sep;
		p += 1;
		
		if (pent->d_type == DT_DIR)
			*p = '1';
		else
			*p = '0';
		p ++;
		*p = sep;
		p ++;
		}
	
	closedir (pdir);
	return buff;
	}


// retrive extension from a full path. !! NOT THREAD SAFE !!
char * fsop_GetExtension (char *path)
	{
	static char buff[256] = {0};
	
	if (!path || !*path) 
		{
		*buff = 0;
		return buff;
		}
	
	char *e = path + strlen(path) - 1;
	
	while (e > path)
		{
		if (*e == '.')
			{
			e++;
			strcpy (buff, e);
			
			return buff;
			}
		e--;
		}

	return NULL;
	}
	
// retrive filename from a full path. !! NOT THREAD SAFE !!
char * fsop_GetFilename (char *path, bool killExt)
	{
	static char fn[256] = {0};
	char buff[256];
	int i = 0;
	
	if (!path || !*path) 
		{
		*fn = 0;
		return fn;
		}
	
	strcpy (buff, path);
	
	// remove extension
	if (killExt)
		{
		i = strlen(buff)-1;
		while (i > 0 && buff[i] != '.')
			{
			i--;
			}
		if (buff[i] == '.') buff[i] = 0;
		}
	
	i = strlen(buff)-1;
	if (i < 0)
		{
		strcpy (fn, buff);
		return fn;
		}
		
	while (i > 0 && buff[i] != '/')
		{
		i--;
		}
		
	if (buff[i] == '/') i++;
	
	strcpy (fn, &buff[i]);
	
	return fn;
	}

// retrive path from a full path. !! NOT THREAD SAFE !!
char * fsop_GetPath (char *path, int killDev)
	{
	static char fn[256] = {0};
	
	if (!path || !*path) 
		{
		*fn = 0;
		return fn;
		}
	
	if (!killDev)
		strcpy (fn, path);
	else
		{
		char *p = strstr (path, "//");
		if (p)
			strcpy (fn, ++p);
		else
			strcpy (fn, path);
		}
	
	int i;
	
	i = strlen(fn)-1;
	while (i > 0 && fn[i] != '/')
		{
		i--;
		}
	
	fn[i] = 0;
	
	return fn;
	}

// retrive dev from a full path. !! NOT THREAD SAFE !!
char * fsop_GetDev (char *path)
	{
	static char fn[256] = {0};
	
	if (!path || !*path) 
		{
		*fn = 0;
		return fn;
		}
	
	strcpy (fn, path);
	
	char *p = strstr (fn, ":");
	if (!p) return NULL;
	
	*p = '\0';

	return fn;
	}


// read a file from disk
u8 *fsop_ReadFile (char *path, size_t bytes2read, size_t *bytesReaded)
	{
	FILE *f;
	size_t size = 0;
	
	f = fopen(path, "rb");
	if (!f)
		{
		if (bytesReaded) *bytesReaded = size;
		return NULL;
		}

	//Get file size
	fseek( f, 0, SEEK_END);
	size = ftell(f);
	
	if (size == 0) 
		{
		fclose (f);
		return NULL;
		}
	
	// goto to start
	fseek( f, 0, SEEK_SET);
	
	if (bytes2read > 0 && bytes2read < size)
		size = bytes2read;
	
	u8 *buff = malloc (size+1);
	size = fread (buff, 1, size, f);
	fclose (f);
	
	buff[size] = 0; // maybe we have readed a ascii file, let's 0 terminate it...
	
	if (bytesReaded) *bytesReaded = size;

	return buff;
	}

// write a buffer to disk
bool fsop_WriteFile (char *path, u8 *buff, size_t len)
	{
	FILE *f;
	size_t size = 0;
	
	f = fopen(path, "wb");
	if (!f)
		{
		return false;
		}

	size = fwrite (buff, 1, len, f);
	fclose (f);

	if (size == len) return true;
	return false;
	}

// return false if the file doesn't exist
bool fsop_GetFileSizeBytes (char *path, size_t *filesize)	// for me stats st_size report always 0 :(
	{
	FILE *f;
	size_t size = 0;
	
	f = fopen(path, "rb");
	if (!f)
		{
		if (filesize) *filesize = size;
		return false;
		}

	//Get file size
	fseek( f, 0, SEEK_END);
	size = ftell(f);
	if (filesize) *filesize = size;
	fclose (f);
	
	//Debug ("fsop_GetFileSizeBytes (%s) = %u", path, size);
	
	return true;
	}

/*

*/
u32 fsop_CountDirItems (char *source)
	{
	DIR *pdir;
	struct dirent *pent;
	u32 count = 0;
	
	pdir=opendir(source);
	
	while ((pent=readdir(pdir)) != NULL) 
		{
		// Skip it
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0)
			continue;

		count++;
		}
	
	closedir(pdir);
	
	return count;
	}

/*
Recursive fsop_GetFolderBytes
*/
u64 fsop_GetFolderBytes (char *source, fsopCallback vc)
	{
	DIR *pdir;
	struct dirent *pent;
	char newSource[300];
	u64 bytes = 0;
	
	pdir=opendir(source);
	
	while ((pent=readdir(pdir)) != NULL) 
		{
		// Skip it
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0)
			continue;
			
		sprintf (newSource, "%s/%s", source, pent->d_name);
		
		// If it is a folder... recurse...
		if (fsop_DirExist (newSource))
			{
			bytes += fsop_GetFolderBytes (newSource, vc);
			}
		else	// It is a file !
			{
			size_t s;
			Debug ("fsop_GetFolderBytes->checking %s", newSource);
			fsop_GetFileSizeBytes (newSource, &s);
			bytes += s;

			if (vc) vc();
			}
		}
	
	closedir(pdir);
	
	//Debug ("fsop_GetFolderBytes (%s) = %llu", source, bytes);
	
	return bytes;
	}

u32 fsop_GetFolderKb (char *source, fsopCallback vc)
	{
	u32 ret = (u32) round ((double)fsop_GetFolderBytes (source, vc) / 1000.0);

	//Debug ("fsop_GetFolderKb (%s) = %u", source, ret);

	return ret;
	}

u32 fsop_GetFreeSpaceKb (char *path) // Return free kb on the device passed
	{
	struct statvfs s;
	
	statvfs (path, &s);
	
	u32 ret = (u32)round( ((double)s.f_bfree / 1000.0) * (double)s.f_bsize);
	
	Debug ("fsop_GetFreeSpaceKb (%s) = %u", path, ret);
	
	return ret ;
	}


bool fsop_StoreBuffer (char *fn, u8 *buff, int size, fsopCallback vc)
	{
	FILE * f;
	int bytes;
	
	f = fopen(fn, "wb");
	if (!f) return false;
	
	bytes = fwrite (buff, 1, size, f);
	fclose(f);
	
	if (bytes == size) return true;
	
	return false;
	}

bool fsop_FileExist (char *fn)
	{
	FILE * f;
	f = fopen(fn, "rb");
	if (!f) return false;
	fclose(f);
	return true;
	}
	
bool fsop_DirExist (char *path)
	{
	DIR *dir;
	
	dir=opendir(path);
	if (dir)
		{
		closedir(dir);
		return true;
		}
	
	return false;
	}

//////////////////////////////////////////////////////////////////////////////////////
#define STACKSIZE	8192
static u8 *copybuff = NULL;
static FILE *fs = NULL, *ft = NULL;
static volatile u32 block = 32768;
static volatile u32 blockIdx = 0;
static volatile u32 blockInfo[2] = {0,0};
static volatile u32 blockReady = 0;
static volatile u32 stopThread;

static void *thread_CopyFileReader (void *arg)
	{
	u32 rb;
	
	stopThread = 0;
	
	do
		{
		SET (rb, fread(&copybuff[blockIdx*block], 1, block, fs ));
		SET (blockInfo[blockIdx], rb);
		SET (blockReady, 1);
		
		while (blockReady && !stopThread) usleep(1);
		}
	while (stopThread == 0);
	
	stopThread = -1;
	
	return NULL;
	}

bool fsop_CopyFile (char *source, char *target, fsopCallback vc)
	{
	block = (32768*2);
	int err = 0;
	fsop.breakop = 0;
	
	u32 size;
	u32 bytes, rb,wb;
	u32 vcskip, ms;
	bool threaded = 1;
	
	Debug ("fsop_CopyFile (%s, %s): Started", source, target);
	
	fs = fopen(source, "rb");
	if (!fs)
		{
		Debug ("fsop_CopyFile: Unable to open source file");
		return false;
		}

	ft = fopen(target, "wt");
	if (!ft)
		{
		fclose (fs);
		Debug ("fsop_CopyFile: Unable to open target file");
		return false;
		}
	
	//Get file size
	fseek ( fs, 0, SEEK_END);
	size = ftell(fs);

	fsop.size = size;
	
	if (size == 0)
		{
		fclose (fs);
		fclose (ft);
		Debug ("fsop_CopyFile: Warning file size 0");
		return true;
		}
		
	// Return to beginning....
	fseek( fs, 0, SEEK_SET);
	
	if (source[0] == target[0] || size < (1024 * 1024 * 10))
		{
		Debug ("fsop_CopyFile: buffer size changed to %dKbyte", block / 1024);
		block *= 2; // grow blocksize
		threaded = 0; // do not use threaded mode on the same device
		}
		
	bytes = 0;
	vcskip = 0;
	if (!threaded) // Use
		{
		copybuff = memalign( 32, block);  
		if (copybuff == NULL) 
			{
			fclose (fs);
			Debug ("fsop_CopyFile: ERR Unable to allocate buffers");
			return false;
			}

		do
			{
			rb = fread(copybuff, 1, block, fs );
			wb = fwrite(copybuff, 1, rb, ft );
			
			if (wb != wb) err = 1;
			if (rb == 0) err = 1;
			bytes += rb;
			
			fsop.multy.bytes += rb;
			fsop.bytes = bytes;
			
			ms = ticks_to_millisecs(gettime());
			if (ms > vcskip && vc) 
				{
				fsop.multy.elapsed = ms - fsop.multy.startms;
				vc();
				vcskip = ticks_to_millisecs(gettime()) + 200;
				}
				
			if (fsop.breakop) break;
			}
		while (bytes < size && err == 0);
		}
	else
		{
		Debug ("fsop_CopyFile: using threaded mode");
		
		u8 * threadStack = NULL;
		lwp_t hthread = LWP_THREAD_NULL;
		
		copybuff = memalign( 32, block * 2);  // We use doublebuffer
		if (copybuff == NULL) 
			{
			fclose (fs);
			Debug ("fsop_CopyFile: ERR Unable to allocate buffers");
			return false;
			}

		blockIdx = 0;
		blockReady = 0;
		blockInfo[0] = 0;
		blockInfo[1] = 0;
		
		Debug ("fsop_CopyFile: prep");
		threadStack = (u8 *) memalign(32, STACKSIZE);
		LWP_CreateThread (&hthread, thread_CopyFileReader, NULL, threadStack, STACKSIZE, 30);
		
		u32 bi;
		do
			{
			while (!blockReady && !fsop.breakop) usleep (1); // Let's wait for incoming block from the thread
			if (fsop.breakop) break;
			
			bi = blockIdx;

			// let's th thread to read the next buff
			SET (blockIdx, 1 - blockIdx);
			SET (blockReady, 0);

			rb = blockInfo[bi];
			// write current block
			wb = fwrite(&copybuff[bi*block], 1, rb, ft );
			
			if (wb != wb) err = 1;
			if (rb == 0) err = 1;
			bytes += rb;
			
			fsop.multy.bytes += rb;
			fsop.bytes = bytes;
			
			ms = ticks_to_millisecs(gettime());
			if (ms > vcskip && vc) 
				{
				fsop.multy.elapsed = ms - fsop.multy.startms;
				vc();
				vcskip = ticks_to_millisecs(gettime()) + 200;
				}
			}
		while (bytes < size && err == 0);
		
		Debug ("fsop_CopyFile: stopping");
		
		stopThread = 1;
		
		while (stopThread != -1)
			{
			usleep (5);
			}

		Debug ("fsop_CopyFile: LWP_JoinThread");
		LWP_JoinThread (hthread, NULL);
		}

	fclose (fs);
	fclose (ft);
	
	free (copybuff);
	
	Debug ("fsop_CopyFile: bytes %u, size %u, err %d, breakop %d", bytes, size, err, fsop.breakop);
	
	if (err) unlink (target);

	if (fsop.breakop || err) return false;
	
	return true;
	}

/*
Semplified folder make
*/
int fsop_MakeFolder (char *path)
	{
	int ret;
	ret = mkdir(path, S_IREAD | S_IWRITE);
	
	Debug ("mkdir %s = %d", path, ret);
	if (ret == 0) return true;
	
	return false;
	}

/*
Recursive copyfolder
*/
static bool doCopyFolder (char *source, char *target, fsopCallback vc)
	{
	DIR *pdir;
	struct dirent *pent;
	char newSource[300], newTarget[300];
	bool ret = true;
	
	// If target folder doesn't exist, create it !
	if (!fsop_DirExist (target))
		{
		fsop_CreateFolderTree (target);
		}

	pdir=opendir(source);
	
	while ((pent=readdir(pdir)) != NULL && ret == true) 
		{
		// Skip it
		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0)
			continue;
			
		sprintf (newSource, "%s/%s", source, pent->d_name);
		sprintf (newTarget, "%s/%s", target, pent->d_name);
		
		// If it is a folder... recurse...
		if (fsop_DirExist (newSource))
			{
			ret = doCopyFolder (newSource, newTarget, vc);
			}
		else	// It is a file !
			{
			strcpy (fsop.op, pent->d_name);
			ret = fsop_CopyFile (newSource, newTarget, vc);
			}
		}
	
	closedir(pdir);

	return ret;
	}
	
bool fsop_CopyFolder (char *source, char *target, fsopCallback vc)
	{
	fsop.breakop = 0;
	fsop.multy.startms = ticks_to_millisecs(gettime());
	fsop.multy.bytes = 0;
	fsop.multy.size = fsop_GetFolderBytes (source, vc);
	
	Debug ("fsop_CopyFolder");
	Debug ("fsop_CopyFolder: source '%s'", source);
	Debug ("fsop_CopyFolder: target '%s'", target);
	Debug ("fsop.multy.startms = %u", fsop.multy.startms);
	Debug ("fsop.multy.bytes = %llu", fsop.multy.bytes);
	Debug ("fsop.multy.size = %llu (%u Mb)", fsop.multy.size, (u32)((fsop.multy.size/1000)/1000));
	
	return doCopyFolder (source, target, vc);
	}
	
/*
Recursive copyfolder
*/
bool fsop_KillFolderTree (char *source, fsopCallback vc)
	{
	DIR *pdir;
	struct dirent *pent;
	char newSource[300];
	
	pdir=opendir(source);
	
	//Debug ("fsop_KillFolderTree: '%s' 0x%08X", source, pdir);
	
	while ((pent=readdir(pdir)) != NULL) 
		{
		// Skip it
		// Debug ("fsop_KillFolderTree: checking '%s'", pent->d_name);

		if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0)
			continue;
			
		sprintf (newSource, "%s/%s", source, pent->d_name);
		
		// If it is a folder... recurse...
		if (fsop_DirExist (newSource))
			{
			fsop_KillFolderTree (newSource, vc);
			}
		else	// It is a file !
			{
			sprintf (fsop.op, "Removing %s", pent->d_name);
			unlink (newSource);
			
			if (vc) vc();
			Debug ("fsop_KillFolderTree: removing '%s'", newSource);
			}
		}
	
	closedir(pdir);
	
	unlink (source);
	
	return true;
	}
	

// Pass  <mount>://<folder1>/<folder2>.../<folderN> or <mount>:/<folder1>/<folder2>.../<folderN>
bool fsop_CreateFolderTree (char *path)
	{
	int i;
	int start, len;
	char buff[300];
	char b[8];
	char *p;
	
	start = 0;
	
	strcpy (b, "://");
	p = strstr (path, b);
	if (p == NULL)
		{
		strcpy (b, ":/");
		p = strstr (path, b);
		if (p == NULL)
			return false; // path must contain
		}
	
	start = (p - path) + strlen(b);
	
	len = strlen(path);
	Debug ("fsop_CreateFolderTree (%s, %d, %d)", path, start, len);

	for (i = start; i <= len; i++)
		{
		if (path[i] == '/' || i == len)
			{
			strcpy (buff, path);
			buff[i] = 0;

			fsop_MakeFolder(buff);
			}
		}
	
	// Check if the tree has been created
	return fsop_DirExist (path);
	}
	
	
// Count the number of folder in a full path. It can be path1/path2/path3 or <mount>://path1/path2/path3
int fsop_CountFolderTree (char *path)
	{
	int i;
	int start, len;
	char b[8];
	char *p;
	int count = 0;
	
	start = 0;
	
	strcpy (b, "://");
	p = strstr (path, b);
	if (p == NULL)
		{
		strcpy (b, ":/");
		p = strstr (path, b);
		}
	
	if (p == NULL)
		start = 0;
	else
		start = (p - path) + strlen(b);
	
	len = strlen(path);
	if (path[len-1] == '/') len--;
	if (len <= 0) return 0;

	for (i = start; i <= len; i++)
		{
		if (path[i] == '/' || i == len)
			{
			count ++;
			}
		}
	
	// Check if the tree has been created
	return count;
	}

