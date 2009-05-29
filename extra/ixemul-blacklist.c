
/**
 * Ixemul.library tasks black-listing management routines.
 * Copyright (c) 2009 Diego Casorran <diegocr at users dot sf dot net>
 * 
 * Redistribution  and  use  in  source  and  binary  forms,  with  or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * -  Redistributions  of  source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this  list  of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * -  Neither  the name of the author(s) nor the names of its contributors may
 * be  used  to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND  ANY  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE  DISCLAIMED.   IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
 * CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT  LIMITED  TO,  PROCUREMENT OF
 * SUBSTITUTE  GOODS  OR  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION)  HOWEVER  CAUSED  AND  ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT,  STRICT  LIABILITY,  OR  TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $Id: ixemul-blacklist.c,v 0.1 2009/05/25 20:42:05 diegocr Exp $
 * 
 */

#include <proto/exec.h>
#include <stat.h>

#define BLM_PARANOID
#define BLM_FILENAME	"ENVARC:ixemul-blacklist.db";
#define BLM_VERSION	1
#define BLM_IDENTIFIER	MAKE_ID('B','L','M',BLM_VERSION)

typedef struct BlackListManager
{
	#ifdef BLM_PARANOID
	ULONG MagicID;
	# define BLMMID	0x9ff47356
	#endif
	
	ULONG flags;
	ULONG mtime;
	APTR mempool;
	struct SignalSemaphore memsem;
	struct MinList bltasks;
	
} BlackListManager;

typedef struct BlackListNode
{
	struct MinNode node;
	
	UBYTE flags;
	UBYTE tnlen;
	UBYTE tname[0];
	
} BlackListTask;

typedef enum {
	
	// per-task flags
	BLM_DONOTHING	=	(1L << 0),
	BLM_OPENREQ	=	(1L << 1),
	BLM_KILLTASK	=	(1L << 2),
	
	// Manager flags
	BLM_TRACKALLOC	=	(1L << 16),
	
} BlackListAction;

STATIC BlackListManager * gblblm = NULL;

#if !defined(IsMinListEmpty)
# define IsMinListEmpty(x)     (((x)->mlh_TailPred) == (struct MinNode *)(x))
#endif

#ifndef ITERATE_LIST
#define ITERATE_LIST(list, type, node)				\
	for(node = (type)((struct List *)(list))->lh_Head;	\
		((struct MinNode *)node)->mln_Succ;		\
			node = (type)((struct MinNode *)node)->mln_Succ)
#endif

#ifndef MAKE_ID
# define MAKE_ID(a,b,c,d)	\
	((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif


/**
 * ::CreateBlackListManager
 * 
 * Creates a new Manager instance..
 * 
 * RETURN 0 on success
 */
STATIC LONG CreateBlackListManager( VOID )
{
	gblblm = AllocMem(sizeof(BlackListManager),MEMF_PUBLIC|MEMF_CLEAR);
	
	if( gblblm == NULL )
		return -1;
	
	gblblm->mempool = CreatePool( MEMF_PUBLIC|MEMF_CLEAR, 512, 512 );
	
	if( gblblm->mempool == NULL )
	{
		FreeMem(gblblm,sizeof(BlackListManager));
		return -3;
	}
	
	InitSemaphore(&gblblm->memsem);
	NewList((struct List *)&gblblm->bltasks);
	
	return(0);
}

/**
 * ::blm_init
 * 
 * Quick-handle Manager creation and error checking
 *
 * RETURN 0 on success
 */
static __inline int blm_init( void )
{
	int rc = 0;
	struct stat st;
	
	Forbid();
	if( gblblm == NULL )
		rc = CreateBlackListManager ( ) ;
	Permit();
	
	if( rc != 0 )
		return -2;
	
	#ifdef BLM_PARANOID
	if( gblblm->MagicID != BLMMID )
		return -3;
	#endif
	
	// Check if there were changes to the database..
	if(!stat(BLM_FILENAME,&st) && gblblm->mtime != st.st_mtime)
	{
		if( blm_readdb ( ))
			gblblm->mtime = st.st_mtime;
	}
	
	return(rc);
}

/**
 * ::blm_addtask
 *
 * Add a task to the blacklist.
 * 
 * RETURN 1 on success, <= 0 otherwise
 */
int blm_addtask(char *taskname, int flags)
{
	int rc = 0, len;
	BlackListTask *blt;
	
	if(!(taskname && *taskname) || flags <= BLM_DONOTHING)
		return -1;
	
	if((rc = blm_init()) != 0)
		return(rc);
	
	ObtainSemaphore(&gblblm->memsem);
	if((blt = AllocPooled(gblblm->mempool,sizeof(*blt)+(len=((strlen(taskname)+2)&0xff)))))
	{
		CopyMem( taskname, &blt->tname[0], len -1);
		blt->tnlen = len - 2;
		blt->flags = flags & 0xff;
		
		//assert((&blt->tname[0])[len-1]==0);
		(&blt->tname[0])[len-1] = 0;
		
		AddTail((struct List *)&gblblm->bltasks,(struct Node *)blt);
		rc = 1;
	}
	ReleaseSemaphore(&gblblm->memsem);
	
	return(rc);
}

/**
 * ::blm_remtask
 *
 * Removes a task from the blacklist.
 * 
 * RETURN 1 on success, 0 otherwise
 */
int blm_remtask(char *taskname)
{
	int rc = 0;
	
	Forbid();
	if( gblblm == NULL )
		rc = 1;
	Permit();
	
	if( rc != 0 || IsMinListEmpty(&gblblm->bltasks))
		return(rc);
	
	ObtainSemaphore(&gblblm->memsem);
	
	
	ReleaseSemaphore(&gblblm->memsem);
	
}

/**
 * ::blm_chktask
 * 
 * Check if a task is blacklisted.
 * 
 * RETURN 0 if it is not, action over task otherwise
 */
int blm_chktask(void *task)
{
	struct Task * _task;
	int rc = 0;
	
	Forbid();
	if( gblblm == NULL )
		rc = 1;
	Permit();
	
	if( rc != 0 || IsMinListEmpty(&gblblm->bltasks))
		return(0);
	
	if((_task = (struct Task *)task) == NULL)
	{
		_task = FindTask(NULL);
	}
	
	
}



