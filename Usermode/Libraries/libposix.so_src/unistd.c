/*
 * Acess2 POSIX Emulation Layer
 * - By John Hodge
 * 
 * unistd.c
 * - POSIX->Acess VFS call translation
 */
#include <unistd.h>
#include <acess/sys.h>
#include <stdarg.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>
#include <acess/devices/pty.h>
#include <errno.h>

// === CODE ===
int unlink(const char *pathname)
{
	return _SysUnlink(pathname);
}

int open(const char *path, int openmode, ...)
{
	mode_t	create_mode = 0;
	int openflags = 0;
	
	switch( openmode & O_RDWR )
	{
	case 0:	// Special
		break;
	case O_RDONLY:	openflags |= OPENFLAG_READ;	break;
	case O_WRONLY:	openflags |= OPENFLAG_WRITE;	break;
	case O_RDWR:	openflags |= OPENFLAG_READ|OPENFLAG_WRITE;	break;
	}
	
	if( openmode & O_CREAT ) {
		openflags |= OPENFLAG_CREATE;
		va_list	args;
		va_start(args, openmode);
		create_mode = va_arg(args, mode_t);
		va_end(args);
	}
	
	if( openmode & O_NONBLOCK )
		openflags |= OPENFLAG_NONBLOCK;
	
	int ret = _SysOpen(path, openflags, create_mode);
	_SysDebug("open('%s', 0%o, 0%o) = %i", path, openmode, create_mode, ret);
	return ret;
}

int access(const char *path, int openmode)
{
	errno = EINVAL;
	return -1;
}

int creat(const char *path, mode_t mode)
{
	// TODO: Make native call to do this cheaper
	int fd = _SysOpen(path, OPENFLAG_CREATE, mode);
	if( fd == -1 )	return -1;
	_SysClose(fd);
	return 0;
}

int close(int fd)
{
	_SysClose(fd);
	return 0;
}

ssize_t	write(int fd, const void *buf, size_t count)
{
	return _SysWrite(fd, buf, count);
}

ssize_t	read(int fd, void *buf, size_t count)
{
	return _SysRead(fd, buf, count);
}

int seek(int fd, int whence, off_t dest)
{
	return _SysSeek(fd, whence, dest);
}

off_t lseek(int fd, off_t offset, int whence)
{
	return _SysSeek(fd, whence, offset);
}

off_t tell(int fd)
{
	return _SysTell(fd);
}

int fork(void)
{
	return _SysClone(CLONE_VM, 0);
}

int execv(const char *b, char *v[])
{
	return _SysExecVE(b, v, NULL);
}

int dup(int oldfd)
{
	_SysDebug("libposix: dup() does not share offsets/flags");
	// NOTE: Acess's CopyFD doesn't cause offset sharing
	int ret = _SysCopyFD(oldfd, -1);
	_SysDebug("dup(%i) = %i", oldfd, ret);
	return ret;
}

int dup2(int oldfd, int newfd)
{
	_SysDebug("libposix: dup2() does not share offsets/flags");
	// NOTE: Acess's CopyFD doesn't cause offset sharing
	_SysDebug("dup2(%i,%i)", oldfd, newfd);
	return _SysCopyFD(oldfd, newfd);
}

int chown(const char *path, uid_t owner, gid_t group)
{
	_SysDebug("TODO: chown(%s, %i, %i)", path, owner, group);
	errno = ENOTIMPL;
	return -1;
}
int chmod(const char *path, mode_t mode)
{
	_SysDebug("TODO: chmod(%s, 0%o)", path, mode);
	errno = ENOTIMPL;
	return -1;
}

/*
 * Set session ID to PID
 */
pid_t setsid(void)
{
	// TODO: actual syscall for this
	return _SysGetPID();
}

pid_t getpid(void)
{
	return _SysGetPID();
}

uid_t getuid(void)
{
	return _SysGetUID();
}
gid_t getgid(void)
{
	return _SysGetGID();
}

uid_t geteuid(void)
{
	// TODO: Impliment EUIDs in-kernel?
	return _SysGetUID();
}

int seteuid(uid_t euid)
{
	_SysDebug("TODO: %s", __func__);
	return 0;
}
int setegid(gid_t egid)
{
	_SysDebug("TODO: %s", __func__);
	return 0;
}

unsigned int sleep(unsigned int seconds)
{
	int64_t	start = _SysTimestamp();
	_SysTimedSleep( seconds*1000 );
	return (_SysTimestamp() - start) / 1000;
}

int usleep(useconds_t usec)
{
	_SysTimedSleep( (usec+999)/1000 );
	return 0;
}

unsigned int alarm(unsigned int seconds)
{
	static int64_t	alarm_time;
	if( seconds > 0 )
	{
		alarm_time = _SysTimestamp() + seconds * 1000;
		// TODO: Schedule SIGALRM
	}
	return (alarm_time - _SysTimestamp()) / 1000;
}

int kill(pid_t pid, int signal)
{
	// TODO: Need special handling?
	return _SysKill(pid, signal);
}

int select(int nfd, fd_set *rfd, fd_set *wfd, fd_set *efd, struct timeval *timeout)
{
	long long int	ltimeout = 0, *ltimeoutp = NULL;
	if( timeout )
	{
		ltimeout = timeout->tv_sec*1000 + timeout->tv_usec / 1000;
		ltimeoutp = &ltimeout;
	}
	_SysDebug("select(%i,{0x%x},{0x%x},{0x%x},%lli)",
		nfd, (rfd?rfd->flags[0]:0), (wfd?wfd->flags[0]:0), (efd?efd->flags[0]:0),
		(ltimeoutp ? *ltimeoutp : -1)
		);
	return _SysSelect(nfd, rfd, wfd, efd, ltimeoutp, 0);
}

int pipe(int pipefd[2])
{
	pipefd[0] = _SysOpen("/Devices/fifo/anon", OPENFLAG_READ|OPENFLAG_WRITE);
	pipefd[1] = _SysCopyFD(pipefd[0], -1);
	_SysFDFlags(pipefd[1], OPENFLAG_READ|OPENFLAG_WRITE, OPENFLAG_WRITE);
	_SysDebug("pipe({%i,%i})", pipefd[0], pipefd[1]);
	return 0;
}

int chdir(const char *dir)
{
	return _SysChdir(dir);
}

int rmdir(const char *pathname)
{
//	return _SysUnlink(pathname);
	_SysDebug("TODO: POSIX rmdir('%s')", pathname);
	errno = ENOTIMPL;
	return -1;
}

int mkdir(const char *pathname, mode_t mode)
{
	_SysDebug("TODO: POSIX mkdir('%s', 0%o)", pathname, mode);
	_SysMkDir(pathname);
	return 0;
}

char *getpass(const char *prompt)
{
	static char passbuf[PASS_MAX+1];
	struct ptymode	oldmode, mode;
	_SysIOCtl(STDIN_FILENO, PTY_IOCTL_GETMODE, &oldmode);
	mode.InputMode = PTYIMODE_CANON;
	mode.OutputMode = 0;
	_SysIOCtl(STDIN_FILENO, PTY_IOCTL_SETMODE, &mode);
	fprintf(stderr, "%s", prompt);
	fflush(stdin);	// clear stdin buffer
	fgets(passbuf, PASS_MAX+1, stdin);
	fprintf(stderr, "\n");
	for( int i = strlen(passbuf); i > 0 && (passbuf[i-1] == '\r' || passbuf[i-1] == '\n'); i -- )
		passbuf[i-1] = 0;

	_SysIOCtl(STDIN_FILENO, PTY_IOCTL_SETMODE, &oldmode);

	return passbuf;
}

char *ttyname(int fd)
{
	static char ttyname_buf[32];
	errno = ttyname_r(fd, ttyname_buf, sizeof(ttyname_buf));
	if(errno)	return NULL;
	
	return ttyname_buf;
}
int ttyname_r(int fd, char *buf, size_t buflen)
{
	 int	type = _SysIOCtl(fd, DRV_IOCTL_TYPE, NULL);
	if( type == -1 )
		return errno;
	if( type != DRV_TYPE_TERMINAL )
		return ENOTTY;

	_SysIOCtl(fd, PTY_IOCTL_GETID, NULL);	

	_SysDebug("TODO: ttyname_r");

	return ENOTIMPL;
}
