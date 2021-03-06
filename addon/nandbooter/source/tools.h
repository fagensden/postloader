/*******************************************************************************
 * tools.h
 *
 * Copyright (c) 2009 The Lemon Man
 * Copyright (c) 2009 Nicksasa
 * Copyright (c) 2009 WiiPower
 *
 * Distributed under the terms of the GNU General Public License (v2)
 * See http://www.gnu.org/licenses/gpl-2.0.txt for more info.
 *
 * Description:
 * -----------
 *
 ******************************************************************************/

#define TITLE_UPPER(x)		((u32)((x) >> 32))
#define TITLE_LOWER(x)		((u32)(x))
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))

bool Power_Flag;
bool Reset_Flag;

void Console_SetPosition(u8 row, u8 column);
void ClearScreen ();
void PrintCenter(char *text, int width);

void *allocate_memory(u32 size);
s32 identify(u64 titleid, u32 *ios);
void debug2Console(bool enable);
void debug(const char *text, ...);
void tell_cIOS_to_return_to_channel(void);
u64 old_title_id;