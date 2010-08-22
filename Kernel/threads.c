/*
 * Acess2
 * threads.c
 * - Common Thread Control
 */
#include <acess.h>
#include <threads.h>
#include <errno.h>

#define DEBUG_TRACE_TICKETS	0

// === CONSTANTS ===
#define	DEFAULT_QUANTUM	10
#define	DEFAULT_TICKETS	5
#define MAX_TICKETS		10
const enum eConfigTypes	cCONFIG_TYPES[] = {
	CFGT_HEAPSTR,	// e.g. CFG_VFS_CWD
	CFGT_INT,	// e.g. CFG_VFS_MAXFILES
	CFGT_NULL
};

// === IMPORTS ===
extern void	ArchThreads_Init(void);
extern void	Proc_Start(void);
extern tThread	*Proc_GetCurThread(void);
extern int	Proc_Clone(Uint *Err, Uint Flags);
extern void	Proc_CallFaultHandler(tThread *Thread);

// === PROTOTYPES ===
void	Threads_Init(void);
 int	Threads_SetName(char *NewName);
char	*Threads_GetName(int ID);
void	Threads_SetTickets(tThread *Thread, int Num);
tThread	*Threads_CloneTCB(Uint *Err, Uint Flags);
 int	Threads_WaitTID(int TID, int *status);
tThread	*Threads_GetThread(Uint TID);
void	Threads_AddToDelete(tThread *Thread);
tThread	*Threads_int_GetPrev(tThread **List, tThread *Thread);
void	Threads_Exit(int TID, int Status);
void	Threads_Kill(tThread *Thread, int Status);
void	Threads_Yield(void);
void	Threads_Sleep(void);
 int	Threads_Wake(tThread *Thread);
void	Threads_AddActive(tThread *Thread);
 int	Threads_GetPID(void);
 int	Threads_GetTID(void);
tUID	Threads_GetUID(void);
 int	Threads_SetUID(Uint *Errno, tUID ID);
tGID	Threads_GetGID(void);
 int	Threads_SetGID(Uint *Errno, tUID ID);
void	Threads_Dump(void);
void	Mutex_Acquire(tMutex *Mutex);
void	Mutex_Release(tMutex *Mutex);
 int	Mutex_IsLocked(tMutex *Mutex);

// === GLOBALS ===
// -- Core Thread --
// Only used for the core kernel
tThread	gThreadZero = {
	Status: THREAD_STAT_ACTIVE,	// Status
	ThreadName:	"ThreadZero",	// Name
	Quantum: DEFAULT_QUANTUM,	// Default Quantum
	Remaining:	DEFAULT_QUANTUM,	// Current Quantum
	NumTickets:	DEFAULT_TICKETS	// Number of tickets
	};
// -- Processes --
// --- Locks ---
tShortSpinlock	glThreadListLock;	///\note NEVER use a heap function while locked
// --- Current State ---
volatile int	giNumActiveThreads = 0;
//volatile int	giTotalTickets = 0;
volatile int	giFreeTickets = 0;
volatile Uint	giNextTID = 1;
// --- Thread Lists ---
tThread	*gActiveThreads = NULL;		// Currently Running Threads
tThread	*gSleepingThreads = NULL;	// Sleeping Threads
tThread	*gDeleteThreads = NULL;		// Threads to delete
 int	giNumCPUs = 1;

// === CODE ===
/**
 * \fn void Threads_Init(void)
 * \brief Initialse the thread list
 */
void Threads_Init(void)
{
	ArchThreads_Init();
	
	// Create Initial Task
	gActiveThreads = &gThreadZero;
	//giFreeTickets = gThreadZero.NumTickets;
	giNumActiveThreads = 1;
		
	Proc_Start();
}

/**
 * \fn void Threads_SetName(char *NewName)
 * \brief Sets the current thread's name
 */
int Threads_SetName(char *NewName)
{
	tThread	*cur = Proc_GetCurThread();
	char	*oldname = cur->ThreadName;
	
	cur->ThreadName = NULL;
	
	if( IsHeap(oldname) )	free( oldname );
	
	cur->ThreadName = strdup(NewName);
	return 0;
}

/**
 * \fn char *Threads_GetName(int ID)
 * \brief Gets a thread's name
 */
char *Threads_GetName(int ID)
{
	if(ID == -1) {
		return Proc_GetCurThread()->ThreadName;
	}
	// TODO: Find a thread and get its name
	return NULL;
}

/**
 * \fn void Threads_SetTickets(tThread *Thread, int Num)
 * \brief Sets the 'priority' of a task
 */
void Threads_SetTickets(tThread *Thread, int Num)
{
	if(Thread == NULL)
		Thread = Proc_GetCurThread();
	if(Num < 0)	return;
	if(Num > MAX_TICKETS)	Num = MAX_TICKETS;
	
	if( Thread != Proc_GetCurThread() ) {
		SHORTLOCK( &glThreadListLock );
		giFreeTickets -= Thread->NumTickets - Num;
		Thread->NumTickets = Num;
		SHORTREL( &glThreadListLock );
	}
	else
		Thread->NumTickets = Num;
}

/**
 * \fn tThread *Threads_CloneTCB(Uint *Err, Uint Flags)
 */
tThread *Threads_CloneTCB(Uint *Err, Uint Flags)
{
	tThread	*cur, *new;
	 int	i;
	cur = Proc_GetCurThread();
	
	new = malloc(sizeof(tThread));
	if(new == NULL) {
		*Err = -ENOMEM;
		return NULL;
	}
	memcpy(new, cur, sizeof(tThread));
	
	new->CurCPU = -1;
	new->Next = NULL;
	memset( &new->IsLocked, 0, sizeof(new->IsLocked));
	new->Status = THREAD_STAT_ACTIVE;
	new->RetStatus = 0;
	
	// Get Thread ID
	new->TID = giNextTID++;
	new->PTID = cur->TID;
	
	// Clone Name
	new->ThreadName = strdup(cur->ThreadName);
	
	// Set Thread Group ID (PID)
	if(Flags & CLONE_VM)
		new->TGID = new->TID;
	else
		new->TGID = cur->TGID;
	
	// Messages are not inherited
	new->Messages = NULL;
	new->LastMessage = NULL;
	
	// Set State
	new->Remaining = new->Quantum = cur->Quantum;
	new->NumTickets = cur->NumTickets;
	
	// Set Signal Handlers
	new->CurFaultNum = 0;
	new->FaultHandler = cur->FaultHandler;
	
	for( i = 0; i < NUM_CFG_ENTRIES; i ++ )
	{
		switch(cCONFIG_TYPES[i])
		{
		default:
			new->Config[i] = cur->Config[i];
			break;
		case CFGT_HEAPSTR:
			if(cur->Config[i])
				new->Config[i] = (Uint) strdup( (void*)cur->Config[i] );
			else
				new->Config[i] = 0;
			break;
		}
	}
	
	return new;
}

/**
 * \fn Uint *Threads_GetCfgPtr(int Id)
 */
Uint *Threads_GetCfgPtr(int Id)
{
	if(Id < 0 || Id >= NUM_CFG_ENTRIES) {
		Warning("Threads_GetCfgPtr: Index %i is out of bounds", Id);
		return NULL;
	}
	
	return &Proc_GetCurThread()->Config[Id];
}

/**
 * \fn void Threads_WaitTID(int TID, int *status)
 * \brief Wait for a task to change state
 */
int Threads_WaitTID(int TID, int *status)
{	
	// Any Child
	if(TID == -1) {
		
		return -1;
	}
	
	// Any peer/child thread
	if(TID == 0) {
		
		return -1;
	}
	
	// TGID = abs(TID)
	if(TID < -1) {
		return -1;
	}
	
	// Specific Thread
	if(TID > 0) {
		tThread	*t = Threads_GetThread(TID);
		 int	initStatus = t->Status;
		 int	ret;
		
		if(initStatus != THREAD_STAT_ZOMBIE) {
			while(t->Status == initStatus) {
				Threads_Yield();
			}
		}
		
		ret = t->RetStatus;
		switch(t->Status)
		{
		case THREAD_STAT_ZOMBIE:
			t->Status = THREAD_STAT_DEAD;
			if(status)	*status = 0;
			Threads_AddToDelete( t );
			break;
		default:
			if(status)	*status = -1;
			break;
		}
		return ret;
	}
	
	return -1;
}

/**
 * \fn tThread *Threads_GetThread(Uint TID)
 * \brief Gets a thread given its TID
 * \param TID	Thread ID
 */
tThread *Threads_GetThread(Uint TID)
{
	tThread *thread;
	
	// Search Active List
	for(thread = gActiveThreads;
		thread;
		thread = thread->Next)
	{
		if(thread->TID == TID)
			return thread;
	}
	
	// Search Sleeping List
	for(thread = gSleepingThreads;
		thread;
		thread = thread->Next)
	{
		if(thread->TID == TID)
			return thread;
	}
	
	return NULL;
}

/**
 * \fn void Threads_AddToDelete(tThread *Thread)
 * \brief Adds a thread to the delete queue
 */
void Threads_AddToDelete(tThread *Thread)
{
	// Add to delete queue
	if(gDeleteThreads) {
		Thread->Next = gDeleteThreads;
		gDeleteThreads = Thread;
	} else {
		Thread->Next = NULL;
		gDeleteThreads = Thread;
	}
}

/**
 * \fn tThread *Threads_int_GetPrev(tThread **List, tThread *Thread)
 * \brief Gets the previous entry in a thead linked list
 */
tThread *Threads_int_GetPrev(tThread **List, tThread *Thread)
{
	tThread *ret;
	// First Entry
	if(*List == Thread) {
		return (tThread*)List;
	} else {
		for(ret = *List;
			ret->Next && ret->Next != Thread;
			ret = ret->Next
			);
		// Error if the thread is not on the list
		if(!ret->Next || ret->Next != Thread) {
			return NULL;
		}
	}
	return ret;
}

/**
 * \fn void Threads_Exit(int TID, int Status)
 * \brief Exit the current process
 */
void Threads_Exit(int TID, int Status)
{
	if( TID == 0 )
		Threads_Kill( Proc_GetCurThread(), (Uint)Status & 0xFF );
	else
		Threads_Kill( Threads_GetThread(TID), (Uint)Status & 0xFF );
	// Halt forever, just in case
	for(;;)
		HALT();
}

/**
 * \fn void Threads_Kill(tThread *Thread, int Status)
 * \brief Kill a thread
 * \param Thread	Thread to kill
 * \param Status	Status code to return to the parent
 */
void Threads_Kill(tThread *Thread, int Status)
{
	tThread	*prev;
	tMsg	*msg;
	
	// Kill all children
	#if 0
	{
		tThread	*child;
		for(child = gActiveThreads;
			child;
			child = child->Next)
		{
			if(child->PTID == Thread->TID)
				Threads_Kill(child, -1);
		}
	}
	#endif
	
	///\note Double lock is needed due to overlap of lock areas
	
	// Lock thread (stop us recieving messages)
	SHORTLOCK( &Thread->IsLocked );
	
	// Lock thread list
	SHORTLOCK( &glThreadListLock );
	
	// Get previous thread on list
	prev = Threads_int_GetPrev( &gActiveThreads, Thread );
	if(!prev) {
		Warning("Proc_Exit - Current thread is not on the active queue");
		Thread->IsLocked.Lock = 0;	// We can't use SHORTREL as that starts IRQs again
		SHORTREL( &glThreadListLock );
		return;
	}
	
	// Clear Message Queue
	while( Thread->Messages )
	{
		msg = Thread->Messages->Next;
		free( Thread->Messages );
		Thread->Messages = msg;
	}
	
	Thread->Remaining = 0;	// Clear Remaining Quantum
	Thread->Quantum = 0;	// Clear Quantum to indicate dead thread
	prev->Next = Thread->Next;	// Remove from active
	
	giNumActiveThreads --;
	if( Thread != Proc_GetCurThread() )
		giFreeTickets -= Thread->NumTickets;
	//Log("Threads_Kill: giFreeTickets = %i", giFreeTickets);
	
	// Mark thread as a zombie
	Thread->RetStatus = Status;
	
	// Don't Zombie if we are being killed as part of a tree
	if(Status == -1)
	{
		Thread->Status = THREAD_STAT_DEAD;
		Threads_AddToDelete( Thread );
	} else {
		Thread->Status = THREAD_STAT_ZOMBIE;
	}
	
	// Release spinlocks
	Thread->IsLocked.Lock = 0;	// Released first so that it IS released
	SHORTREL( &glThreadListLock );
	
	//Log("Thread %i went *hurk*", Thread->TID);
	
	if(Status != -1)	HALT();
}

/**
 * \fn void Threads_Yield(void)
 * \brief Yield remainder of timeslice
 */
void Threads_Yield(void)
{
	Proc_GetCurThread()->Remaining = 0;
	HALT();
}

/**
 * \fn void Threads_Sleep(void)
 * \brief Take the current process off the run queue
 */
void Threads_Sleep(void)
{
	tThread *cur = Proc_GetCurThread();
	tThread *thread;
	
	//Log_Log("Threads", "%i going to sleep", cur->TID);
	
	// Acquire Spinlock
	SHORTLOCK( &glThreadListLock );
	
	// Get thread before current thread
	thread = Threads_int_GetPrev( &gActiveThreads, cur );
	if(!thread) {
		Warning("Threads_Sleep - Current thread is not on the active queue");
		Threads_Dump();
		SHORTREL( &glThreadListLock );
		return;
	}
	
	// Don't sleep if there is a message waiting
	if( cur->Messages ) {
		SHORTREL( &glThreadListLock );
		return;
	}
	
	// Unset remaining timeslices (force a task switch on timer fire)
	cur->Remaining = 0;
	
	// Remove from active list
	thread->Next = cur->Next;
	
	// Add to Sleeping List (at the top)
	cur->Next = gSleepingThreads;
	gSleepingThreads = cur;
	
	// Reduce the active count & ticket count
	giNumActiveThreads --;
	// - No need to alter giFreeTickets (we're being executed)
	
	// Mark thread as sleeping
	cur->Status = THREAD_STAT_SLEEPING;
	
	// Release Spinlock
	SHORTREL( &glThreadListLock );
	
	while(cur->Status != THREAD_STAT_ACTIVE)	HALT();
}


/**
 * \fn int Threads_Wake( tThread *Thread )
 * \brief Wakes a sleeping/waiting thread up
 * \param Thread	Thread to wake
 * \return Boolean Failure (Returns ERRNO)
 */
int Threads_Wake(tThread *Thread)
{
	tThread	*prev;
	
	if(!Thread)
		return -EINVAL;
	
	switch(Thread->Status)
	{
	case THREAD_STAT_ACTIVE:
		Log("Thread_Wake: Waking awake thread (%i)", Thread->TID);
		return -EALREADY;
	case THREAD_STAT_SLEEPING:	// TODO: Comment better
		//Log_Log("Threads", "Waking %i (%p) from sleeping (CPU=%i)",
		//	Thread->TID, Thread, Thread->CurCPU);
		SHORTLOCK( &glThreadListLock );
		prev = Threads_int_GetPrev(&gSleepingThreads, Thread);
		prev->Next = Thread->Next;	// Remove from sleeping queue
		Thread->Next = gActiveThreads;	// Add to active queue
		gActiveThreads = Thread;
		giNumActiveThreads ++;
		// Thread can't be the current, so no need to check
		Thread->CurCPU = -1;
		giFreeTickets += Thread->NumTickets;
		#if DEBUG_TRACE_TICKETS
		Log("Threads_Wake: giFreeTickets = %i", giFreeTickets);
		#endif
		Thread->Status = THREAD_STAT_ACTIVE;
		SHORTREL( &glThreadListLock );
		return -EOK;
	case THREAD_STAT_WAITING:
		Warning("Thread_Wake - Waiting threads are not currently supported");
		return -ENOTIMPL;
	case THREAD_STAT_DEAD:
		Warning("Thread_Wake - Attempt to wake dead thread (%i)", Thread->TID);
		return -ENOTIMPL;
	default:
		Warning("Thread_Wake - Unknown process status (%i)\n", Thread->Status);
		return -EINTERNAL;
	}
}

/**
 * \brief Wake a thread given the TID
 * \param TID	Thread ID to wake
 * \return Boolean Faulure (errno)
 */
int Threads_WakeTID(tTID TID)
{
	tThread	*thread = Threads_GetThread(TID);
	if(!thread)
		return -ENOENT;
	return Threads_Wake( thread );
}

/**
 * \fn void Threads_AddActive(tThread *Thread)
 * \brief Adds a thread to the active queue
 */
void Threads_AddActive(tThread *Thread)
{
	SHORTLOCK( &glThreadListLock );
	Thread->Next = gActiveThreads;
	gActiveThreads = Thread;
	giNumActiveThreads ++;
	// Thread can't be the current, so no need to check
	giFreeTickets += Thread->NumTickets;
	#if DEBUG_TRACE_TICKETS
	Log("Threads_AddActive: giFreeTickets = %i", giFreeTickets);
	#endif
	SHORTREL( &glThreadListLock );
}

/**
 * \fn void Threads_SetFaultHandler(Uint Handler)
 * \brief Sets the signal handler for a signal
 */
void Threads_SetFaultHandler(Uint Handler)
{	
	Log_Log("Threads", "Threads_SetFaultHandler: Handler = %p", Handler);
	Proc_GetCurThread()->FaultHandler = Handler;
}

/**
 * \fn void Threads_Fault(int Num)
 * \brief Calls a fault handler
 */
void Threads_Fault(int Num)
{
	tThread	*thread = Proc_GetCurThread();
	
	Log_Log("Threads", "Threads_Fault: thread = %p", thread);
	
	if(!thread)	return ;
	
	Log_Log("Threads", "Threads_Fault: thread->FaultHandler = %p", thread->FaultHandler);
	
	switch(thread->FaultHandler)
	{
	case 0:	// Panic?
		Threads_Kill(thread, -1);
		HALT();
		return ;
	case 1:	// Dump Core?
		Threads_Kill(thread, -1);
		HALT();
		return ;
	}
	
	// Double Fault? Oh, F**k
	if(thread->CurFaultNum != 0) {
		Threads_Kill(thread, -1);	// For now, just kill
		HALT();
	}
	
	thread->CurFaultNum = Num;
	
	Proc_CallFaultHandler(thread);
}

// --- Process Structure Access Functions ---
tPID Threads_GetPID(void)
{
	return Proc_GetCurThread()->TGID;
}
tTID Threads_GetTID(void)
{
	return Proc_GetCurThread()->TID;
}
tUID Threads_GetUID(void)
{
	return Proc_GetCurThread()->UID;
}
tGID Threads_GetGID(void)
{
	return Proc_GetCurThread()->GID;
}

int Threads_SetUID(Uint *Errno, tUID ID)
{
	tThread	*t = Proc_GetCurThread();
	if( t->UID != 0 ) {
		*Errno = -EACCES;
		return -1;
	}
	Log("Threads_SetUID - Setting User ID to %i", ID);
	t->UID = ID;
	return 0;
}

int Threads_SetGID(Uint *Errno, tGID ID)
{
	tThread	*t = Proc_GetCurThread();
	if( t->UID != 0 ) {
		*Errno = -EACCES;
		return -1;
	}
	Log("Threads_SetGID - Setting Group ID to %i", ID);
	t->GID = ID;
	return 0;
}

/**
 * \fn void Threads_Dump(void)
 * \brief Dums a list of currently running threads
 */
void Threads_Dump(void)
{
	tThread	*thread;
	
	Log("Active Threads:");
	for(thread=gActiveThreads;thread;thread=thread->Next)
	{
		Log(" %i (%i) - %s (CPU %i)",
			thread->TID, thread->TGID, thread->ThreadName, thread->CurCPU);
		Log("  %i Tickets, Quantum %i", thread->NumTickets, thread->Quantum);
		Log("  KStack 0x%x", thread->KernelStack);
	}
	Log("Sleeping Threads:");
	for(thread=gSleepingThreads;thread;thread=thread->Next)
	{
		Log(" %i (%i) - %s",
			thread->TID, thread->TGID, thread->ThreadName);
		Log("  %i Tickets, Quantum %i", thread->NumTickets, thread->Quantum);
		Log("  KStack 0x%x", thread->KernelStack);
	}
}

/**
 * \fn tThread *Threads_GetNextToRun(int CPU, tThread *Last)
 * \brief Gets the next thread to run
 * \param CPU	Current CPU
 * \param Last	The thread the CPU was running
 */
tThread *Threads_GetNextToRun(int CPU, tThread *Last)
{
	tThread	*thread;
	 int	ticket;
	 int	number;
	
	// TODO: Enable the code to tell if the current CPU has the lock or
	//       another does.
	
	// Check if the thread list is locked by other code
	// - If so, don't switch (give it a chance to complete)
	if( IS_LOCKED(&glThreadListLock) )
		return Last;
	
	// Clear Delete Queue
	while(gDeleteThreads)
	{
		thread = gDeleteThreads->Next;
		if( IS_LOCKED(&gDeleteThreads->IsLocked) ) {	// Only free if structure is unused
			gDeleteThreads->Status = THREAD_STAT_NULL;
			free( gDeleteThreads );
		}
		gDeleteThreads = thread;
	}
	
	// No active threads, just take a nap
	if(giNumActiveThreads == 0) {
		#if DEBUG_TRACE_TICKETS
		Log("No active threads");
		#endif
		return NULL;
	}
	
	// Lock thread list
	// - HLT lock (Used because only another CPU can obtain the lock,
	//   but it has a potentially long lock period)
	// - Well, this CPU can obtain the lock, but that is aliveviated by
	//   the above.
	SHORTLOCK( &glThreadListLock );
	
	// Special case: 1 thread
	if(giNumActiveThreads == 1) {
		if( gActiveThreads->CurCPU == -1 )
			gActiveThreads->CurCPU = CPU;
		SHORTREL( &glThreadListLock );
		if( gActiveThreads->CurCPU == CPU )
			return gActiveThreads;
		return NULL;	// CPU has nothing to do
	}
	
	// Allow the old thread to be scheduled again
	if( Last ) {
		if( Last->Status == THREAD_STAT_ACTIVE ) {
			giFreeTickets += Last->NumTickets;
			#if DEBUG_TRACE_TICKETS
			LogF(" CPU %i released %p (%s) into the pool (%i tickets in pool)\n",
				CPU, Last, Last->ThreadName, Last->NumTickets);
			#endif
		}
		#if DEBUG_TRACE_TICKETS
		else
			LogF(" %p (%s)->Status = %i\n", Last, Last->ThreadName, Last->Status);
		#endif
		Last->CurCPU = -1;
	}
	
	#if 1
	number = 0;
	for(thread=gActiveThreads;thread;thread=thread->Next) {
		if(thread->CurCPU >= 0)	continue;
		number += thread->NumTickets;
	}
	if(number != giFreeTickets) {
		Panic("Bookkeeping fail (giFreeTickets(%i) != number(%i)) - CPU%i",
			giFreeTickets, number, CPU);
	}
	#endif
	
	// No free tickets (all tasks delegated to cores)
	if( giFreeTickets == 0 ) {
		SHORTREL(&glThreadListLock);
		return NULL;
	}
	
	// Get the ticket number
	ticket = number = rand() % giFreeTickets;
	
	// Find the next thread
	for(thread=gActiveThreads;thread;thread=thread->Next)
	{
		if(thread->CurCPU >= 0)	continue;
		if(thread->NumTickets > number)	break;
		number -= thread->NumTickets;
	}
	// Error Check
	if(thread == NULL)
	{
		number = 0;
		for(thread=gActiveThreads;thread;thread=thread->Next) {
			if(thread->CurCPU >= 0)	continue;
			number += thread->NumTickets;
		}
		Panic("Bookeeping Failed - giFreeTickets(%i) > true count (%i)",
			giFreeTickets, number);
	}
	#if DEBUG_TRACE_TICKETS
	LogF(" CPU%i giFreeTickets = %i\n", CPU, giFreeTickets);
	#endif
	
	// Make the new thread non-schedulable
	giFreeTickets -= thread->NumTickets;	
	thread->CurCPU = CPU;
	
	//Threads_Dump();
	#if DEBUG_TRACE_TICKETS
	LogF(" CPU%i giFreeTickets = %i, giving %p (%s CPU=%i)\n",
		CPU, giFreeTickets, thread, thread->ThreadName, thread->CurCPU);
	#endif
	
	SHORTREL( &glThreadListLock );
	
	return thread;
}

/**
 * \fn void Threads_SegFault(tVAddr Addr)
 * \brief Called when a Segment Fault occurs
 */
void Threads_SegFault(tVAddr Addr)
{
	Warning("Thread #%i committed a segfault at address %p", Proc_GetCurThread()->TID, Addr);
	Threads_Fault( 1 );
	//Threads_Exit( 0, -1 );
}

/**
 * \brief Acquire a heavy mutex
 * \param Mutex	Mutex to acquire
 * 
 * This type of mutex checks if the mutex is avaliable, and acquires it
 * if it is. Otherwise, the current thread is added to the mutex's wait
 * queue and the thread suspends. When the holder of the mutex completes,
 * the oldest thread (top thread) on the queue is given the lock and
 * restarted.
 */
void Mutex_Acquire(tMutex *Mutex)
{
	tThread	*us = Proc_GetCurThread();
	tThread	*prev;
	
	// Get protector
	SHORTLOCK( &Mutex->Protector );
	
	//Log("Mutex_Acquire: (%p)", Mutex);
	
	// Check if the lock is already held
	if( Mutex->Owner ) {
		SHORTLOCK( &glThreadListLock );
		// - Remove from active list
		us->Remaining = 0;
		prev = Threads_int_GetPrev(&gActiveThreads, us);
		prev->Next = us->Next;
		giNumActiveThreads --;
		us->Status = THREAD_STAT_SLEEPING;
		
		// - Add to waiting
		if(Mutex->LastWaiting) {
			Mutex->LastWaiting->Next = us;
			Mutex->LastWaiting = us;
		}
		else {
			Mutex->Waiting = us;
			Mutex->LastWaiting = us;
		}
		SHORTREL( &glThreadListLock );
		SHORTREL( &Mutex->Protector );
		while(us->Status == THREAD_STAT_SLEEPING)	HALT();
		// We're only woken when we get the lock
	}
	// Ooh, let's take it!
	else {
		Mutex->Owner = us;
		SHORTREL( &Mutex->Protector );
	}
}

/**
 * \brief Release a held mutex
 * \param Mutex	Mutex to release
 */
void Mutex_Release(tMutex *Mutex)
{
	SHORTLOCK( &Mutex->Protector );
	//Log("Mutex_Release: (%p)", Mutex);
	if( Mutex->Waiting ) {
		Mutex->Owner = Mutex->Waiting;	// Set owner
		Mutex->Waiting = Mutex->Waiting->Next;	// Next!
		// Wake new owner
		Mutex->Owner->Status = THREAD_STAT_ACTIVE;
		Threads_AddActive(Mutex->Owner);
		Log("Mutex %p Woke %p", Mutex, Mutex->Owner);
	}
	else {
		Mutex->Owner = NULL;
	}
	SHORTREL( &Mutex->Protector );
}

/**
 * \brief Is this mutex locked?
 * \param Mutex	Mutex pointer
 */
int Mutex_IsLocked(tMutex *Mutex)
{
	return Mutex->Owner != NULL;
}

// === EXPORTS ===
EXPORT(Threads_GetUID);
