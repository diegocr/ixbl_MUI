/* ***** BEGIN LICENSE BLOCK *****
 * Version: BSD License
 * 
 * Copyright (c) 2009, Diego Casorran <dcasorran@gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 ** Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  
 ** Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * ***** END LICENSE BLOCK ***** */

asm(".globl __start\njbsr __start\nrts");

#define __NOLIBBASE__
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <workbench/startup.h>
#include <dos/dostags.h>
#include <SDI_hook.h>
#include <stdarg.h>

/// stolen from ixemul.h --->
   #define ix_kill_app_on_failed_malloc                 0x00100000
   #define ix_catch_failed_malloc                       0x00080000
/// <--- stolen from ixemul.h

//---------------------------------------------------------------------------
/**
 * $Id: ixbl_MUI.c,v 0.1 2009/05/29 20:14:35 diegocr Exp $
 * 
 * $ChangeLog:
 * 
 * 	0.1 [20090529] · First Version
 */
STATIC CONST STRPTR ApplicationData[] =
{
	"IXBL",							/* title / base */
	"$VER: IXBL 0.1 (29.05.2009) ©2009 Diego Casorran",	/* Version */
	"Ixemul.library Blacklist",				/* Description */
	NULL
};

#define BLM_FILENAME	"ENVARC:ixemul-blacklist.db"
#define BLM_VERSION	1
#define BLM_IDENTV1	MAKE_ID('B','L','M',BLM_VERSION)
#define BLM_IDENTIFIER	BLM_IDENTV1

//---------------------------------------------------------------------------

static struct Library * SysBase       = NULL;
static struct Library * UtilityBase   = NULL;
static struct Library * DOSBase       = NULL;
static struct Library * IntuitionBase = NULL;
       struct Library * MUIMasterBase = NULL;

GLOBAL Object * IXBLogo ( VOID );

#ifndef MAKE_ID
# define MAKE_ID(a,b,c,d)	\
	((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif

typedef struct
{
	Object * app;
	Object * window;
	Object * LV_Main;
	Object * LI_List;
	
	Object * add;
	Object * prog;
	Object * killappnomem;
	Object * catchfailalloc;
	
	BOOL db_changed;
	
} ProgramData;

#define malloc(s)	AllocVec((s),MEMF_PUBLIC|MEMF_CLEAR)
#define free		FreeVec
//#define strdup	_sDup

#if !defined(IsMinListEmpty)
# define IsMinListEmpty(x)     (((x)->mlh_TailPred) == (struct MinNode *)(x))
#endif

#define ITERATE_LIST(list, type, node)				\
	for (node = (type)((struct List *)(list))->lh_Head;	\
		((struct MinNode *)node)->mln_Succ;		\
			node = (type)((struct MinNode *)node)->mln_Succ)

//---------------------------------------------------------------------------

STATIC VOID IRequestA( CONST_STRPTR fmt, APTR args )
{
	struct EasyStruct es = {
		sizeof (struct EasyStruct), 0, FindTask(NULL)->tc_Node.ln_Name,
		(STRPTR) fmt, "OK"
	};
	
	DisplayBeep(NULL);
	EasyRequestArgs( NULL, &es, NULL, args );
}
#define IRequest(fmt,args...)	\
	({ULONG _args[] = { args }; IRequestA( fmt, (APTR)_args);})

#define Tell(fmt...)	IRequest(fmt)
#define TellA(fmt...)	IRequestA(fmt)

#define xget(OBJ, ATTR) ({ULONG b=0; GetAttr(ATTR, OBJ, &b); b;})
#define _GBool(obj)	(!!xget(obj,MUIA_Selected))
#define _GCycV(obj)	(xget(obj,MUIA_Cycle_Active))
#define _GNBut(obj)	(xget(obj,MUIA_Numeric_Value))
#define _GRadV(obj)	(xget(obj,MUIA_Radio_Active))
#define _GSli(obj)	(xget(obj,MUIA_Slider_Level))
#define _GStr(obj)	((STRPTR)xget(obj,MUIA_String_Contents))

//---------------------------------------------------------------------------

#define strlen __strlen
STATIC ULONG strlen(REG(a0,const char *string))
{
	register const char *s=string;
	
	if(!(string && *string))
		return 0;
	
	do;while(*s++); return ~(string-s);
}

#define strchr __strchr
STATIC char *strchr(REG(a0,const char *s),REG(d0,int c))
{
  while (*s!=(char)c)
    if (!*s++)
      { s = (char *)0; break; }
  return (char *)s;
}

#define strcpy _strcpy
STATIC VOID strcpy(REG(a0,char *dst),REG(a1,char *src))
{
	while((*dst++ = *src++) != 0);
}

#define sprintf _sprintf
STATIC VOID sprintf(char *dest,const char *fmt, ...)
{
	STATIC CONST ULONG tricky=0x16c04e75;
	va_list args;
	
	va_start(args,fmt);
	RawDoFmt(fmt,(APTR)args,(void (*)())&tricky,dest);
	va_end(args);
}

//---------------------------------------------------------------------------

struct lEntry
{
	ULONG Magic;
	#define LEMID 0xFF405909
	
	ULONG flags;
	UBYTE tname[255];
	UBYTE flfmt[12];
};

static const struct lEntry *lEntryAdd(ULONG flags,STRPTR name)
{
	static struct lEntry le;
	
	le.Magic = LEMID;
	le.flags = flags;
	
	strcpy( le.tname, name );
	sprintf(le.flfmt, "$%08lx", flags );
	
	return((const struct lEntry *) &le );
}

HOOKPROTONH( dispfunc, LONG, char **array, struct lEntry *entry)
{
	if( entry )
	{
		*array++ = entry->tname;
		*array = entry->flfmt;
	}
	else
	{
		*array++ = "\033u\033bTask Name";
		*array = "\033u\033bOptions";
	}
	return(0);
}
MakeStaticHook( disphook, dispfunc );

HOOKPROTONH( consfunc, APTR, APTR pool, struct lEntry *entry)
{
	struct lEntry *new_entry = NULL;
	
	if((entry == NULL) || entry->Magic != LEMID)
		return NULL;
	
	if((new_entry = (struct lEntry *)AllocPooled(pool, sizeof(struct lEntry))))
	{
		*new_entry = *entry;
		
		new_entry->Magic >>= 16;
	}
	
	return(new_entry);
}
MakeStaticHook( conshook, consfunc );

HOOKPROTONH( destfunc, VOID, APTR pool, struct lEntry *entry)
{
	if( entry && entry->Magic >> 16 == LEMID >> 16 )
	{
		entry->Magic = 0xDEAD;
		FreePooled(pool, entry, sizeof(struct lEntry));
	}
}
MakeStaticHook( desthook, destfunc );

//---------------------------------------------------------------------------

STATIC VOID LI_List_SelectNone(ProgramData *data)
{
	set( data->LI_List, MUIA_List_Active, MUIV_List_Active_Off );
	setstring( data->prog, (ULONG)"");
	setcheckmark(data->catchfailalloc,0);
	setcheckmark(data->killappnomem,0);
}

HOOKPROTONONP( AddEntry, VOID )
{
	ProgramData * data = (ProgramData *) hook->h_Data;
	STRPTR progname;
	ULONG flags = 0;
	struct lEntry *e;
	long pos = 0;
	
	if(!(progname = _GStr( data->prog )) || !*progname)
	{
		DisplayBeep(NULL);
		return;
	}
	
	// check if this prog comes from an edit
	while(TRUE)
	{
		DoMethod( data->LI_List, MUIM_List_GetEntry, pos++, &e );
		if(!e) break;
		
		if(!Stricmp( e->tname, progname ))
			break;
	}
	
	if(_GBool( data->catchfailalloc ))
		flags |= ix_catch_failed_malloc;
	
	if(_GBool( data->killappnomem ))
		flags |= ix_kill_app_on_failed_malloc;
	
	if( e == NULL )
	{
		DoMethod(data->LI_List, MUIM_List_InsertSingle,
			lEntryAdd(flags,progname),MUIV_List_Insert_Bottom);
	}
	else
	{
		e = (void *)lEntryAdd(flags,e->tname);
		
		DoMethod(data->LI_List, MUIM_List_InsertSingle,e,MUIV_List_Insert_Active);
		DoMethod(data->LI_List, MUIM_List_Remove, MUIV_List_Remove_Active );
		LI_List_SelectNone(data);
	}
	data->db_changed = TRUE;
}
MakeStaticHook( AddEntryHook, AddEntry );

//---------------------------------------------------------------------------

HOOKPROTONONP( RemEntry, VOID )
{
	ProgramData * data = (ProgramData *) hook->h_Data;
	
	DoMethod( data->LI_List, MUIM_List_Remove, MUIV_List_Remove_Active );
	data->db_changed = TRUE;
}
MakeStaticHook( RemEntryHook, RemEntry );

//---------------------------------------------------------------------------

HOOKPROTONONP( EditEntry, VOID )
{
	ProgramData * data = (ProgramData *) hook->h_Data;
	struct lEntry *e;
	
	DoMethod( data->LI_List, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &e );
	if(!e) return;
	
	setstring( data->prog,(ULONG) e->tname );
	setcheckmark(data->catchfailalloc,(e->flags & ix_catch_failed_malloc));
	setcheckmark(data->killappnomem,(e->flags & ix_kill_app_on_failed_malloc));
}
MakeStaticHook( EditEntryHook, EditEntry );

//---------------------------------------------------------------------------

static int LoadDatabase_V1(BPTR fd,ProgramData *data)
{
	int rc = 0;
	ULONG ul;
	UBYTE name[256], len;
	
	while(TRUE)
	{
		if(Read(fd, &ul, sizeof(ULONG)) != sizeof(ULONG))
			return -3;
		
		if( ul == MAKE_ID('.','E','O','F'))
		{
			rc = 1; /* everything OK. */
			break;
		}
		
		if(Read(fd, &len, sizeof(UBYTE)) != sizeof(UBYTE))
			return -4;
		
		if(Read(fd, name, len ) != len )
			return -5;
		
		DoMethod(data->LI_List, MUIM_List_InsertSingle,
			lEntryAdd(ul,name),MUIV_List_Insert_Bottom);
	}
	
	return(rc);
}

static int SaveDatabase_V1(BPTR fd,ProgramData *data)
{
	struct lEntry *e;
	LONG pos = 0;
	int rc = 0;
	UBYTE l;
	
	while(TRUE)
	{
		DoMethod( data->LI_List, MUIM_List_GetEntry, pos++, &e );
		if(!e) break;
		
		if(Write( fd, &e->flags, sizeof(ULONG)) != sizeof(ULONG))
			return -1;
		
		l = strlen(e->tname) + 1;
		if(Write( fd, &l, sizeof(UBYTE)) != sizeof(UBYTE))
			return -2;
		
		if(Write( fd, e->tname, l ) != l )
			return -3;
		
		rc++;
	}
	
	return(rc);
}

static int SaveDatabase(ProgramData *data)
{
	BPTR fd;
	int rc = 0;
	
	if((fd = Open( BLM_FILENAME, MODE_NEWFILE )))
	{
		ULONG id = BLM_IDENTIFIER;
		
		if(Write( fd, &id, sizeof(ULONG)) == sizeof(ULONG))
			rc = SaveDatabase_V1(fd,data);
		
		if( rc > 0 )
		{
			id = MAKE_ID('.','E','O','F');
			Write( fd, &id, sizeof(ULONG));
		}
		
		Close(fd);
	}
	
	return(rc);
}

static int LoadDatabase(ProgramData *data)
{
	BPTR fd;
	int rc = 0;
	
	if((fd = Open( BLM_FILENAME, MODE_OLDFILE )))
	{
		ULONG id;
		
		if(Read(fd, &id, sizeof(ULONG))==sizeof(ULONG))
		{
			switch( id )
			{
				case BLM_IDENTIFIER:
					rc = LoadDatabase_V1(fd,data);
					break;
				
				default:
					rc = -2;
					break;
			}
		}
		else rc = -1;
		
		Close(fd);
	}
	
	return(rc);
}

//---------------------------------------------------------------------------

INLINE LONG Main( void )
{
	ProgramData * data;
	LONG rc = RETURN_FAIL;
	
	if(!(data = malloc(sizeof(ProgramData))))
		return(rc);
	
	data->app = ApplicationObject,
		MUIA_Application_Title      , ApplicationData[0],
		MUIA_Application_Version    , ApplicationData[1],
		MUIA_Application_Copyright  , ApplicationData[1] + 29,
		MUIA_Application_Author     , ApplicationData[1] + 36,
		MUIA_Application_Description, ApplicationData[2],
		MUIA_Application_Base       , ApplicationData[0],
		
		SubWindow, data->window = WindowObject,
			MUIA_Window_Title, ApplicationData[2],
			MUIA_Window_ID, MAKE_ID('A','S','I','U'),
			WindowContents, VGroup,
				MUIA_Background,(ULONG)"2:00000000,00000000,00000000",
				InnerSpacing(0,0),
				Child, IXBLogo ( ),
				
				Child, VGroup,
					InnerSpacing(8,0),
					Child, MUI_MakeObject(MUIO_HBar,2),
					Child, ColGroup(2),
					//	GroupFrame,
					//	MUIA_Background, MUII_GroupBack,
						Child, Label2("\0338Program:"),
						Child, HGroup,
							Child, data->prog = StringObject,
								StringFrame,
								MUIA_String_MaxLen, 254,
							End,
							Child, data->add = MUI_MakeObject(MUIO_Button, "_Add"),
						End,
					//	Child, VGroup,
							Child, Label2("\0338Options:\n\n"),
					//		Child, HVSpace,
					//	End,
						Child, VGroup,
							Child, ColGroup(3),
								Child, data->catchfailalloc = MUI_MakeObject(MUIO_Checkmark, NULL),
								Child, LLabel1("\0338Catch Failed Allocations."),
								Child, HVSpace,
								Child, data->killappnomem = MUI_MakeObject(MUIO_Checkmark, NULL),
								Child, LLabel1("\0338Kill Task on failed allocation."),
								Child, HVSpace,
							End,
					/*		Child, HGroup,
								Child, MUI_MakeObject(MUIO_Checkmark, NULL),
								Child, Label1(""),
							End,
					*/	End,
					End,
					
					Child, MUI_MakeObject(MUIO_HBar,2),
					
					Child, data->LV_Main = ListviewObject,
						MUIA_Background, MUII_ListBack,
 						MUIA_Listview_Input, TRUE,
 						MUIA_Listview_List, data->LI_List = ListObject,
 							InputListFrame,
 							MUIA_List_Title, TRUE,
 							MUIA_List_ConstructHook, &conshook,
 							MUIA_List_DestructHook, &desthook,
 							MUIA_List_DisplayHook, &disphook,
 							MUIA_List_Format, "BAR,",
 						End,
 					End,
 					
 					Child, VSpace(2),
 				End,
			End,
		End,
	End;
	
	if( data->app == NULL )
	{
		TellA("Unable to create application",NULL);
	}
	else
	{
		AddEntryHook.h_Data = (APTR) data;
		RemEntryHook.h_Data = (APTR) data;
		EditEntryHook.h_Data = (APTR) data;
		
		DoMethod(data->window,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,
			MUIV_Notify_Application,2,MUIM_Application_ReturnID,MUIV_Application_ReturnID_Quit);
		
		DoMethod( data->add, MUIM_Notify, MUIA_Pressed, FALSE,
			MUIV_Notify_Self, 2, MUIM_CallHook, &AddEntryHook );
		
		DoMethod( data->LV_Main, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
			MUIV_Notify_Self, 3, MUIM_CallHook, &RemEntryHook );
		
		DoMethod( data->LI_List, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
			MUIV_Notify_Self, 2, MUIM_CallHook, &EditEntryHook );
		
	//	DoMethod(data->LI_List, MUIM_List_InsertSingle,
	//		lEntryAdd(0xC00,"prOgr4m"),MUIV_List_Insert_Bottom);
		
		set( data->prog, MUIA_ShortHelp,(ULONG)""
			"The Task (name) to be Blacklisted.\n"
			"Thats only the containing text (either WB\n"
			"or CLI program name) where a single string\n"
			"matching will be performed (using strstr() func)");
		
		set( data->catchfailalloc, MUIA_ShortHelp,(ULONG)""
			"When malloc() returns 0, open a requester to inform of the issue.\n"
			"If the global option (ixprefs) is enabled, you can disable the\n"
			"requester over the blacklisted task.");
		
		set( data->killappnomem, MUIA_ShortHelp,(ULONG)""
			"Enabling this option will cause the blacklisted application to be\n"
			"terminated when malloc() returns 0, but before a requester will be\n"
			"show if the above option is selected. On that requester you'll have\n"
			"two options, 'Try Again' and 'Ignore', Clicking Try Again will cause\n"
			"ixemul to retry the allocation, if the allocation cannot be yet\n"
			"possible you'll see the same requester again, and so on.\n"
			"Cliking Ignore will cause the application to be abort()'ed, but\n"
			"NOTE thats something \"dangerous\" and may not work system-friendly\n"
			"on certain application... If the global option (ixprefs) is enabled,\n"
			"you can disable killing the blacklisted task.");
		
		set( data->LI_List, MUIA_ShortHelp,(ULONG)""
			"This is the list of blacklisted applications,\n"
			"it's simple but powerfull ;-)\n\n"
			"Click once over a entry to edit it, and DoubleClick\n"
			"to remove it. The list is saved at program exit.");
		
		set(data->window,MUIA_Window_Open,TRUE);
		if(xget(data->window,MUIA_Window_Open) == FALSE)
		{
			TellA("Cannot open window!",NULL);
		}
		else
		{
			ULONG sigs = 0;
			
			LoadDatabase ( data ) ;
			
			while (DoMethod(data->app,MUIM_Application_NewInput,&sigs) != (ULONG)MUIV_Application_ReturnID_Quit)
			{
				if (sigs)
				{
					sigs = Wait(sigs | SIGBREAKF_CTRL_C);
					if (sigs & SIGBREAKF_CTRL_C) break;
				}
			}
			
			rc = RETURN_OK;
			
			if( data->db_changed )
			{
				set( data->app, MUIA_Application_Sleep, TRUE );
				if( SaveDatabase ( data ) < 0 )
					DisplayBeep(NULL);
			}
		}
		
		MUI_DisposeObject(data->app);
	}
	
	free(data);
	
	return(rc);
}

//---------------------------------------------------------------------------

LONG _start( void )
{
	struct Task * me;
	LONG rc = RETURN_ERROR;
	struct WBStartup * _WBenchMsg = NULL;
	
	SysBase = *(struct Library **) 4L;
	
	if(!((struct Process *)(me=FindTask(NULL)))->pr_CLI)
	{
		struct MsgPort * mp = &((struct Process *)me)->pr_MsgPort;
		
		WaitPort(mp);
		
		_WBenchMsg = (struct WBStartup *)GetMsg(mp);
	}
	
	if((DOSBase = OpenLibrary("dos.library", 36)))
	{
		if((UtilityBase = OpenLibrary("utility.library", 0)))
		{
			if((IntuitionBase = OpenLibrary("intuition.library", 0)))
			{
				if((MUIMasterBase = OpenLibrary("muimaster.library", 19)))
				{
					rc = Main ( ) ;
					
					CloseLibrary( MUIMasterBase );
				}
				else
				{
					TellA("This program requires MUI",NULL);
				}
				
				CloseLibrary( IntuitionBase );
			}
			
			CloseLibrary( UtilityBase );
		}
		
		CloseLibrary( DOSBase );
	}
	
	if( rc == RETURN_ERROR )
		rc = ERROR_INVALID_RESIDENT_LIBRARY;
	
	if(_WBenchMsg != NULL)
	{
		Forbid();
		
		ReplyMsg((struct Message *)_WBenchMsg);
	}
	
	return(rc);
}

void bcopy(const void *src,void *dest,ULONG len)
{
	CopyMem((APTR) src,(APTR) dest, len );
}
