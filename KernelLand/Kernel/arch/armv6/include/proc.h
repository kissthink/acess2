/*
 * Acess2
 * ARM7 Architecture
 *
 * proc.h - Arch-Dependent Process Management
 */
#ifndef _PROC_H_
#define _PROC_H_

#define MAX_CPUS	4
#define USER_MAX	0x80000000

// === STRUCTURES ===
typedef struct {
	Uint32	IP, SP;
	Uint32	UserIP, UserSP;
} tTaskState;

typedef struct {
	Uint32	Base;
} tMemoryState;

typedef struct {
	union {
		Uint32	Num;
		Uint32	Error;
	};
	union {
		Uint32	Arg1;
		Uint32	Return;
	};
	union {
		Uint32	Arg2;
		Uint32	RetHi;
	};
	Uint32	Arg3;
	Uint32	Arg4;
	Uint32	Arg5;
	Uint32	Arg6;	// R6
} tSyscallRegs;

// === MACROS ===
#define HALT()	do{}while(0)

// === PROTOTYPES ===

#endif

