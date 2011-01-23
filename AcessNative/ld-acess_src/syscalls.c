/*
 */
#include "../../Usermode/include/acess/sys.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "request.h"
#include "../syscalls.h"

// === Types ===

// === IMPORTS ===

// === CODE ===
const char *ReadEntry(tRequestValue *Dest, void *DataDest, void **PtrDest, const char *ArgTypes, va_list Args)
{
	uint64_t	val64;
	uint32_t	val32;
	 int	direction = 0;	// 0: Invalid, 1: Out, 2: In, 3: Out
	char	*str;
	 int	len;
	
	// Eat whitespace
	while(*ArgTypes && *ArgTypes == ' ')	ArgTypes ++;
	if( *ArgTypes == '\0' )	return ArgTypes;
	
	// Get direction
	switch(*ArgTypes)
	{
	default:	// Defaults to output
	case '>':	direction = 1;	break;
	case '<':	direction = 2;	break;
	case '?':	direction = 3;	break;
	}
	ArgTypes ++;
	
	// Eat whitespace
	while(*ArgTypes && *ArgTypes == ' ')	ArgTypes ++;
	if( *ArgTypes == '\0' )	return ArgTypes;
	
	// Get type
	switch(*ArgTypes)
	{
	// 32-bit integer
	case 'i':
		
		if( direction != 1 ) {
			fprintf(stderr, "ReadEntry: Recieving an integer is not defined\n");
			return NULL;
		}
		
		val32 = va_arg(Args, uint32_t);
		
		Dest->Type = ARG_TYPE_INT32;
		Dest->Length = sizeof(uint32_t);
		Dest->Flags = 0;
		
		if( DataDest )
			*(uint32_t*)DataDest = val32;
		break;
	// 64-bit integer
	case 'I':
		
		if( direction != 1 ) {
			fprintf(stderr, "ReadEntry: Recieving an integer is not defined\n");
			return NULL;
		}
		
		val64 = va_arg(Args, uint64_t);
		
		Dest->Type = ARG_TYPE_INT64;
		Dest->Length = sizeof(uint64_t);
		Dest->Flags = 0;
		if( DataDest )
			*(uint64_t*)DataDest = val64;
		break;
	// String
	case 's':
		// Input string makes no sense!
		if( direction != 1 ) {
			fprintf(stderr, "ReadEntry: Recieving a string is not defined\n");
			return NULL;
		}
		
		str = va_arg(Args, char*);
		
		Dest->Type = ARG_TYPE_STRING;
		Dest->Length = strlen(str) + 1;
		Dest->Flags = 0;
		
		if( DataDest )
		{
			memcpy(DataDest, str, Dest->Length);
		}
		break;
	// Data (special handling)
	case 'd':
		len = va_arg(Args, int);
		str = va_arg(Args, char*);
		
		// Save the pointer for later
		if( PtrDest )	*PtrDest = str;
		
		// Create parameter block
		Dest->Type = ARG_TYPE_INT64;
		Dest->Length = sizeof(uint64_t);
		Dest->Flags = 0;
		if( direction & 2 )
			Dest->Flags |= ARG_FLAG_RETURN;
		
		// Has data?
		if( direction & 1 )
		{
			if( DataDest )
				memcpy(DataDest, str, len);
		}
		else
			Dest->Flags |= ARG_FLAG_ZEROED;
		break;
	
	default:
		return NULL;
	}
	ArgTypes ++;
	
	return ArgTypes;
}

/**
 * \param ArgTypes
 *
 * Whitespace is ignored
 * >i:	Input Integer (32-bits)
 * >I:	Input Long Integer (64-bits)
 * >s:	Input String
 * >d:	Input Buffer (Preceded by valid size)
 * <I:	Output long integer
 * <d:	Output Buffer (Preceded by valid size)
 * ?d:  Bi-directional buffer (Preceded by valid size), buffer contents
 *      are returned
 */
uint64_t _Syscall(int SyscallID, const char *ArgTypes, ...)
{
	va_list	args;
	 int	paramCount, dataLength;
	 int	retCount = 1, retLength = sizeof(uint64_t);
	void	**retPtrs;	// Pointers to return buffers
	const char	*str;
	tRequestHeader	*req;
	void	*dataPtr;
	uint64_t	retValue;
	 int	i;
	
	// Get data size
	va_start(args, ArgTypes);
	str = ArgTypes;
	paramCount = 0;
	dataLength = 0;
	while(*str)
	{
		tRequestValue	tmpVal;
		
		str = ReadEntry(&tmpVal, NULL, NULL, str, args);
		if( !str ) {
			fprintf(stderr, "syscalls.c: ReadEntry failed (SyscallID = %i)\n", SyscallID);
			exit(127);
		}
		paramCount ++;
		if( !(tmpVal.Flags & ARG_FLAG_ZEROED) )
			dataLength += tmpVal.Length;
		
		if( tmpVal.Flags & ARG_FLAG_RETURN ) {
			retLength += tmpVal.Length;
			retCount ++;
		}
	}
	va_end(args);
	
	dataLength += sizeof(tRequestHeader) + paramCount*sizeof(tRequestValue);
	retLength += sizeof(tRequestHeader) + retCount*sizeof(tRequestValue);
	
	// Allocate buffers
	retPtrs = malloc( sizeof(void*) * (retCount+1) );
	if( dataLength > retLength)
		req = malloc( dataLength );
	else
		req = malloc( retLength );
	req->ClientID = 0;	//< Filled later
	req->CallID = SyscallID;
	req->NParams = paramCount;
	dataPtr = &req->Params[paramCount];
	
	// Fill `output` and `input`
	va_start(args, ArgTypes);
	str = ArgTypes;
	// - re-zero so they can be used as indicies
	paramCount = 0;
	retCount = 0;
	while(*str)
	{		
		str = ReadEntry(&req->Params[paramCount], dataPtr, &retPtrs[retCount], str, args);
		if( !str )	break;
		
		if( !(req->Params[paramCount].Flags & ARG_FLAG_ZEROED) )
			dataPtr += req->Params[paramCount].Length;
		if( req->Params[paramCount].Flags & ARG_FLAG_RETURN )
			retCount ++;
		
		paramCount ++;
	}
	va_end(args);
	
	// Send syscall request
	if( SendRequest(req, dataLength) ) {
		fprintf(stderr, "syscalls.c: SendRequest failed (SyscallID = %i)\n", SyscallID);
		exit(127);
	}
	
	// Parse return value
	dataPtr = &req->Params[req->NParams];
	retValue = 0;
	if( req->NParams > 1 )
	{
		switch(req->Params[0].Type)
		{
		case ARG_TYPE_INT64:
			retValue = *(uint64_t*)dataPtr;
			dataPtr += req->Params[0].Length;
			break;
		case ARG_TYPE_INT32:
			retValue = *(uint32_t*)dataPtr;
			dataPtr += req->Params[0].Length;
			break;
		}	
	}
	
	// Write changes to buffers
	va_start(args, ArgTypes);
	for( i = 1; i < req->NParams; i ++ )
	{
		memcpy( retPtrs[i-1], dataPtr, req->Params[i].Length );
		dataPtr += req->Params[i].Length;
	}
	va_end(args);
	
	free( req );
	
	return 0;
}

// --- VFS Calls
int open(const char *Path, int Flags) {
	return _Syscall(SYS_OPEN, ">s >i", Path, Flags);
}

void close(int FD) {
	_Syscall(SYS_CLOSE, ">i", FD);
}

size_t read(int FD, size_t Bytes, void *Dest) {
	return _Syscall(SYS_READ, "<i >i >i <d", FD, Bytes, Bytes, Dest);
}

size_t write(int FD, size_t Bytes, void *Src) {
	return _Syscall(SYS_WRITE, ">i >i >d", FD, Bytes, Bytes, Src);
}

int seek(int FD, int64_t Ofs, int Dir) {
	return _Syscall(SYS_SEEK, ">i >I >i", FD, Ofs, Dir);
}

uint64_t tell(int FD) {
	return _Syscall(SYS_TELL, ">i", FD);
}

int ioctl(int fd, int id, void *data) {
	 int	ret = 0;
	// NOTE: 1024 byte size is a hack
	_Syscall(SYS_IOCTL, "<i >i >i ?d", &ret, fd, id, 1024, data);
	return ret;
}
int finfo(int fd, t_sysFInfo *info, int maxacls) {
	 int	ret = 0;
	_Syscall(SYS_FINFO, "<i >i <d >i",
		&ret, fd,
		sizeof(t_sysFInfo)+maxacls*sizeof(t_sysACL), info,
		maxacls);
	return ret;
}

int readdir(int fd, char *dest) {
	 int	ret = 0;
	_Syscall(SYS_READDIR, "<i >i <d", &ret, fd, 256, dest);
	return ret;
}

int _SysOpenChild(int fd, char *name, int flags) {
	 int	ret = 0;
	_Syscall(SYS_OPENCHILD, "<i >i >s >i", &ret, fd, name, flags);
	return ret;
}

int _SysGetACL(int fd, t_sysACL *dest) {
	 int	ret = 0;
	_Syscall(SYS_GETACL, "<i >i <d", &ret, fd, sizeof(t_sysACL), dest);
	return ret;
}

int _SysMount(const char *Device, const char *Directory, const char *Type, const char *Options) {
	 int	ret = 0;
	_Syscall(SYS_MOUNT, "<i >s >s >s >s", &ret, Device, Directory, Type, Options);
	return ret;
}


// --- Error Handler
int	_SysSetFaultHandler(int (*Handler)(int)) {
	return 0;
}


// === Symbol List ===
#define DEFSYM(name)	{#name, name}
const tSym	caBuiltinSymbols[] = {
	{"_exit", exit},
	
	DEFSYM(open),
	DEFSYM(close),
	DEFSYM(read),
	DEFSYM(write),
	DEFSYM(seek),
	DEFSYM(tell),
	DEFSYM(ioctl),
	DEFSYM(finfo),
	DEFSYM(readdir),
	DEFSYM(_SysOpenChild),
	DEFSYM(_SysGetACL),
	DEFSYM(_SysMount),
	
	{"_SysSetFaultHandler", _SysSetFaultHandler}
};

const int	ciNumBuiltinSymbols = sizeof(caBuiltinSymbols)/sizeof(caBuiltinSymbols[0]);

