/*
 * Acess2 C Library
 * - By John Hodge (thePowersGang)
 * 
 * errno.c
 * - errno and strerror
 */
#include "lib.h"
#include <stdio.h>
#include <errno.h>
#include <acess/sys.h>
#include <string.h>

EXPORT int *libc_geterrno()
{
	return &_errno;
}

EXPORT char *strerror(int errnum)
{
	switch((enum libc_eErrorNumbers)errnum)
	{
	case EOK:	return "Success";
	case ERANGE:	return "Value out of range";
	case EDOM:	return "Value out of domain";
	case EILSEQ:	return "Illegal character sequence";

	case ENOSYS:	return "Invalid instruction/syscall";
	case EINVAL:	return "Bad argument(s)";
	case EBADF:	return "Invalid file";
	case ENOMEM:	return "No free memory";
	case EACCES:	return "Not permitted";
	case EBUSY:	return "Resource is busy";
	case ENOTFOUND:	return "Item not found";
	case EROFS:	return "Read only filesystem";
	case ENOTIMPL:	return "Not implimented";
	case ENOENT:	return "No such file or directory";
	case EEXIST:	return "Already exists";
	case ENFILE:	return "Too many open files";
	case ENOTDIR:	return "Not a directory";
	case EISDIR:	return "Is a directory";
	case EIO:	return "IO Error";
	case EINTR:	return "Interrupted";
	case EWOULDBLOCK:	return "Operation would have blocked";
	case ENODEV:	return "No such device";
	case EADDRNOTAVAIL:	return "Address not avaliable";
	case EINPROGRESS:	return "Operation in process";
	case EPERM:	return "Operation not permitted";
	case ENOTTY:	return "Not a TTY";
	case EAGAIN:	return "Try again";
	case EFBIG:	return "File too big";
	case E2BIG:	return "Value too big";
	case EALREADY:	return "Operation was no-op";
	case ENOSPC:	return "No space left on the device";

	case EAFNOSUPPORT:	return "Address family not supported";
	case EADDRINUSE:	return "Address already in use";
	case ETIMEDOUT:	return "Operation timed out";
	case EOPNOTSUPP:	return "Operation not supported on socket";

	case EINTERNAL:	return "Internal error";
	}
	_SysDebug("strerror: errnum=%i unk", errnum);
	errno = EINVAL;
	return "unknown error";
}

EXPORT int strerror_r(int errnum, char *buf, size_t bufsiz)
{
	const char *str = strerror(errnum);
	if(!str)
		return -1;
	
	strncpy(buf, str, bufsiz);
	return 0;
}

// stdio.h
EXPORT void perror(const char *s)
{
	int err = errno;
	if( s && s[0] ) {
		fprintf(stderr, "%s: (%i) %s\n", s, err, strerror(err));
	}
	else {
		fprintf(stderr, "(%i) %s\n", err, strerror(err));
	}
	_SysDebug("perror('%s'): %s (%i)", s, strerror(err), err);
}

