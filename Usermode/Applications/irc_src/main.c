/*
 * Acess2 IRC Client
 */
#include <acess/sys.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <net.h>
#include <readline.h>

#define BUFSIZ	1023

// === TYPES ===
typedef struct sServer {
	struct sServer	*Next;
	 int	FD;
	char	InBuf[BUFSIZ+1];
	 int	ReadPos;
	char	Name[];
} tServer;

typedef struct sMessage
{
	struct sMessage	*Next;
	time_t	Timestamp;
	tServer	*Server;
	 int	Type;
	char	*Source;	// Pointer into `Data`
	char	Data[];
}	tMessage;

typedef struct sWindow
{
	struct sWindow	*Next;
	tMessage	*Messages;
	tServer	*Server;	//!< Canoical server (can be NULL)
	 int	ActivityLevel;
	char	Name[];	// Channel name / remote user
}	tWindow;

enum eMessageTypes
{
	MSG_TYPE_NULL,
	MSG_TYPE_SERVER,	// Server message
	
	MSG_TYPE_NOTICE,	// NOTICE command
	MSG_TYPE_JOIN,	// JOIN command
	MSG_TYPE_PART,	// PART command
	MSG_TYPE_QUIT,	// QUIT command
	
	MSG_TYPE_STANDARD,	// Standard line
	MSG_TYPE_ACTION,	// /me
	
	MSG_TYPE_UNK
};

// === PROTOTYPES ===
 int	ParseArguments(int argc, const char *argv[]);
 int	ParseUserCommand(char *String);
// --- 
tServer	*Server_Connect(const char *Name, const char *AddressString, short PortNumber);
tMessage	*Message_Append(tServer *Server, int Type, const char *Source, const char *Dest, const char *Message);
tWindow	*Window_Create(tServer *Server, const char *Name);

 int	ProcessIncoming(tServer *Server);
// --- Helpers
 int	writef(int FD, const char *Format, ...);
 int	OpenTCP(const char *AddressString, short PortNumber);
char	*GetValue(char *Str, int *Ofs);
static inline int	isdigit(int ch);

// === GLOBALS ===
char	*gsUsername = "root";
char	*gsHostname = "acess";
char	*gsRealName = "Acess2 IRC Client";
char	*gsNickname = "acess";
tServer	*gpServers;
tWindow	gWindow_Status = {
	NULL, NULL, NULL,	// No next, empty list, no server
	0, ""	// No activity, empty name (rendered as status)
};
tWindow	*gpWindows = &gWindow_Status;
tWindow	*gpCurrentWindow = &gWindow_Status;

// ==== CODE ====
int main(int argc, const char *argv[], const char *envp[])
{
	 int	tmp;
	tReadline	*readline_info;
	
	// Parse Command line
	if( (tmp = ParseArguments(argc, argv)) )	return tmp;
	
	// HACK: Static server entry
	// UCC (University [of Western Australia] Computer Club) IRC Server
	gWindow_Status.Server = Server_Connect( "UCC", "130.95.13.18", 6667 );
	
	if( !gWindow_Status.Server )
		return -1;
	
	readline_info = Readline_Init(1);
	
	for( ;; )
	{
		fd_set	readfds, errorfds;
		 int	rv, maxFD = 0;
		tServer	*srv;
		
		FD_ZERO(&readfds);
		FD_ZERO(&errorfds);
		FD_SET(0, &readfds);	// stdin
		
		// Fill server FDs in fd_set
		for( srv = gpServers; srv; srv = srv->Next )
		{
			FD_SET(srv->FD, &readfds);
			FD_SET(srv->FD, &errorfds);
			if( srv->FD > maxFD )
				maxFD = srv->FD;
		}
		
		rv = select(maxFD+1, &readfds, 0, &errorfds, NULL);
		if( rv == -1 )	break;
		
		if(FD_ISSET(0, &readfds))
		{
			// User input
			char	*cmd = Readline_NonBlock(readline_info);
			if( cmd )
			{
				if( cmd[0] )
				{
					ParseUserCommand(cmd);
				}
				free(cmd);
			}
		}
		
		// Server response
		for( srv = gpServers; srv; srv = srv->Next )
		{
			if(FD_ISSET(srv->FD, &readfds))
			{
				if( ProcessIncoming(srv) != 0 ) {
					// Oops, error
					break;
				}
			}
			
			if(FD_ISSET(srv->FD, &errorfds))
			{
				break;
			}
		}
		
		// Oops, an error
		if( srv )	break;
	}
	
	{
		tServer *srv;
		for( srv = gpServers; srv; srv = srv->Next )
			close(srv->FD);
	}
	return 0;
}

/**
 * \todo Actually implement correctly :)
 */
int ParseArguments(int argc, const char *argv[])
{
	return 0;
}

int ParseUserCommand(char *String)
{
	if( String[0] == '/' )
	{
		char	*command;
		 int	pos = 0;
		
		command = GetValue(String, &pos);
		
		if( strcmp(command, "/join") == 0 )
		{
			char	*channel_name = GetValue(String, &pos);
			
			if( gpCurrentWindow->Server )
			{
				writef(gpCurrentWindow->Server->FD, "JOIN %s\n",  channel_name);
			}
		}
		else if( strcmp(command, "/quit") == 0 )
		{
			char	*quit_message = GetValue(String, &pos);
			tServer	*srv;
			
			if( !quit_message )
				quit_message = "/quit - Acess2 IRC Client";
			
			for( srv = gpServers; srv; srv = srv->Next )
			{
				writef(srv->FD, "QUIT %s\n", quit_message);
			}
		}
		else if( strcmp(command, "/window") == 0 || strcmp(command, "/win") == 0 || strcmp(command, "/w") == 0 )
		{
			char	*window_id = GetValue(String, &pos);
			 int	window_num = atoi(window_id);
			
			if( window_num > 0 )
			{
				tWindow	*win;
				window_num --;	// Move to base 0
				// Get `window_num`th window
				for( win = gpWindows; win && window_num--; win = win->Next );
				if( win ) {
					gpCurrentWindow = win;
					if( win->Name[0] )
						printf("[%s:%s] ", win->Server->Name, win->Name);
					else
						printf("[(status)] ", win->Server->Name, win->Name);
				}
				// Otherwise, silently ignore
			}
		}
		else
		{
			 int	len = snprintf(NULL, 0, "Unknown command %s", command);
			char	buf[len+1];
			snprintf(buf, len+1, "Unknown command %s", command);
			Message_Append(NULL, MSG_TYPE_SERVER, "client", "", buf);
		}
	}
	else
	{
		// Message
		// - Only send if server is valid and window name is non-empty
		if( gpCurrentWindow->Server && gpCurrentWindow->Name[0] )
		{
			writef(gpCurrentWindow->Server->FD,
				"PRIVMSG %s :%s\n", gpCurrentWindow->Name,
				String
				);
		}
	}
	
	return 0;
}

/**
 * \brief Connect to a server
 */
tServer *Server_Connect(const char *Name, const char *AddressString, short PortNumber)
{
	tServer	*ret;
	
	ret = calloc(1, sizeof(tServer) + strlen(Name) + 1);
	
	strcpy(ret->Name, Name);
	
	// Connect to the remove server
	ret->FD = OpenTCP( AddressString, PortNumber );
	if( ret->FD == -1 ) {
		fprintf(stderr, "%s: Unable to create socket\n", Name);
		return NULL;
	}
	
	// Append to open list
	ret->Next = gpServers;
	gpServers = ret;
	
	// Read some initial data
	printf("%s: Connection opened\n", Name);
	ProcessIncoming(ret);
	
	// Identify
	writef(ret->FD, "USER %s %s %s : %s\n", gsUsername, gsHostname, AddressString, gsRealName);
	writef(ret->FD, "NICK %s\n", gsNickname);
	printf("%s: Identified\n", Name);
	
	return ret;
}

tMessage *Message_Append(tServer *Server, int Type, const char *Source, const char *Dest, const char *Message)
{
	tMessage	*ret;
	tWindow	*win = NULL;
	 int	msgLen = strlen(Message);
	
	// NULL servers are internal messages
	if( Server == NULL )
	{
		win = &gWindow_Status;
	}
	// Determine if it's a channel or PM message
	else if( Dest[0] == '#' || Dest[0] == '&' )	// TODO: Better determining here
	{
		tWindow	*prev = NULL;
		for(win = gpWindows; win; prev = win, win = win->Next)
		{
			if( win->Server == Server && strcmp(win->Name, Dest) == 0 )
			{
				break;
			}
		}
		if( !win ) {
			win = Window_Create(Server, Dest);
		}
	}
	#if 0
	else if( strcmp(Dest, Server->Nick) != 0 )
	{
		// Umm... message for someone who isn't us?
		win = &gWindow_Status;	// Stick it in the status window, just in case
	}
	#endif
	// Server message?
	else if( strchr(Source, '.') )	// TODO: And again, less hack please
	{
		#if 1
		for(win = gpWindows; win; win = win->Next)
		{
			if( win->Server == Server && strcmp(win->Name, Source) == 0 )
			{
				break;
			}
		}
		#endif
		if( !win ) {
			win = &gWindow_Status;
		}
		
	}
	// Private message
	else
	{
		for(win = gpWindows; win; win = win->Next)
		{
			if( win->Server == Server && strcmp(win->Name, Source) == 0 )
			{
				break;
			}
		}
		if( !win ) {
			win = Window_Create(Server, Dest);
		}
	}
	
	ret = malloc( sizeof(tMessage) + msgLen + 1 + strlen(Source) + 1 );
	ret->Source = ret->Data + msgLen + 1;
	strcpy(ret->Source, Source);
	strcpy(ret->Data, Message);
	ret->Type = Type;
	ret->Server = Server;
	
	// TODO: Append to window message list
	ret->Next = win->Messages;
	win->Messages = ret;
	
	return ret;
}

tWindow *Window_Create(tServer *Server, const char *Name)
{
	tWindow	*ret, *prev = NULL;
	 int	num = 1;
	
	// Get the end of the list
	// TODO: Cache this instead
	for( ret = gpCurrentWindow; ret; prev = ret, ret = ret->Next )
		num ++;
	
	ret = malloc(sizeof(tWindow) + strlen(Name) + 1);
	ret->Messages = NULL;
	ret->Server = Server;
	ret->ActivityLevel = 1;
	strcpy(ret->Name, Name);
	
	if( prev ) {
		ret->Next = prev->Next;
		prev->Next = ret;
	}
	else {	// Shouldn't happen really
		ret->Next = gpWindows;
		gpWindows = ret;
	}
	
	printf("Win %i %s:%s created\n", num, Server->Name, Name);
	
	return ret;
}

void Cmd_PRIVMSG(tServer *Server, const char *Dest, const char *Src, const char *Message)
{
	printf("<%s:%s:%s> %s\n", Server->Name, Dest, Src, Message);
}

/**
 */
void ParseServerLine(tServer *Server, char *Line)
{
	 int	pos = 0;
	char	*ident, *cmd;
	
	// Message?
	if( *Line == ':' )
	{
		ident = GetValue(Line, &pos);	// Ident (user or server)
		cmd = GetValue(Line, &pos);
		
		// Numeric command
		if( isdigit(cmd[0]) && isdigit(cmd[1]) && isdigit(cmd[2]) )
		{
			char	*user, *message;
			 int	num;
			num  = (cmd[0] - '0') * 100;
			num += (cmd[1] - '0') * 10;
			num += (cmd[2] - '0') * 1;
			
			user = GetValue(Line, &pos);
			
			if( Line[pos] == ':' ) {
				message = Line + pos + 1;
			}
			else {
				message = GetValue(Line, &pos);
			}
			
			switch(num)
			{
			default:
				printf("[%s] %i %s\n", Server->Name, num, message);
				Message_Append(Server, MSG_TYPE_SERVER, ident, user, message);
				break;
			}
		}
		else if( strcmp(cmd, "NOTICE") == 0 )
		{
			char	*class, *message;
			
			class = GetValue(Line, &pos);
			
			if( Line[pos] == ':' ) {
				message = Line + pos + 1;
			}
			else {
				message = GetValue(Line, &pos);
			}
			
			printf("[%s] NOTICE %s: %s\n", Server->Name, ident, message);
			Message_Append(Server, MSG_TYPE_NOTICE, ident, "", message);
		}
		else if( strcmp(cmd, "PRIVMSG") == 0 )
		{
			char	*dest, *message;
			dest = GetValue(Line, &pos);
			
			if( Line[pos] == ':' ) {
				message = Line + pos + 1;
			}
			else {
				message = GetValue(Line, &pos);
			}
			Cmd_PRIVMSG(Server, dest, ident, message);
			Message_Append(Server, MSG_TYPE_STANDARD, ident, dest, message);
		}
		else
		{
			printf("Unknown message %s (%s)\n", cmd, Line+pos);
		}
	}
	else {
		
		// Command to client
		printf("Client Command: %s", Line);
	}
}

/**
 * \brief Process incoming lines from the server
 */
int ProcessIncoming(tServer *Server)
{	
	char	*ptr, *newline;
	 int	len;
	
	// While there is data in the buffer, read it into user memory and 
	// process it line by line
	// ioctl#8 on a TCP client gets the number of bytes in the recieve buffer
	// - Used to avoid blocking
	#if NON_BLOCK_READ
	while( (len = ioctl(Server->FD, 8, NULL)) > 0 )
	{
	#endif
		// Read data
		len = read(Server->FD, BUFSIZ - Server->ReadPos, &Server->InBuf[Server->ReadPos]);
		if( len == -1 ) {
			return -1;
		}
		Server->InBuf[Server->ReadPos + len] = '\0';
		
		// Break into lines
		ptr = Server->InBuf;
		while( (newline = strchr(ptr, '\n')) )
		{
			*newline = '\0';
			if( newline[-1] == '\r' )	newline[-1] = '\0';
			ParseServerLine(Server, ptr);
			ptr = newline + 1;
		}
		
		// Handle incomplete lines
		if( ptr - Server->InBuf < len + Server->ReadPos ) {
			// Update the read position
			// InBuf ReadPos    ptr          ReadPos+len
			// | old | new used | new unused |
			Server->ReadPos = len + Server->ReadPos - (ptr - Server->InBuf);
			// Copy stuff back (moving "new unused" to the start of the buffer)
			memcpy(Server->InBuf, ptr, Server->ReadPos);
		}
		else {
			Server->ReadPos = 0;
		}
	#if NON_BLOCK_READ
	}
	#endif
	
	return 0;
}

/**
 * \brief Write a formatted string to a file descriptor
 * 
 */
int writef(int FD, const char *Format, ...)
{
	va_list	args;
	 int	len;
	
	va_start(args, Format);
	len = vsnprintf(NULL, 1000, Format, args);
	va_end(args);
	
	{
		char	buf[len+1];
		va_start(args, Format);
		vsnprintf(buf, len+1, Format, args);
		va_end(args);
		
		return write(FD, len, buf);
	}
}

/**
 * \brief Initialise a TCP connection to \a AddressString on port \a PortNumber
 */
int OpenTCP(const char *AddressString, short PortNumber)
{
	 int	fd, addrType;
	char	*iface;
	char	addrBuffer[8];
	
	// Parse IP Address
	addrType = Net_ParseAddress(AddressString, addrBuffer);
	if( addrType == 0 ) {
		fprintf(stderr, "Unable to parse '%s' as an IP address\n", AddressString);
		return -1;
	}
	
	// Finds the interface for the destination address
	iface = Net_GetInterface(addrType, addrBuffer);
	if( iface == NULL ) {
		fprintf(stderr, "Unable to find a route to '%s'\n", AddressString);
		return -1;
	}
	
	printf("iface = '%s'\n", iface);
	
	// Open client socket
	// TODO: Move this out to libnet?
	{
		 int	len = snprintf(NULL, 100, "/Devices/ip/%s/tcpc", iface);
		char	path[len+1];
		snprintf(path, 100, "/Devices/ip/%s/tcpc", iface);
		fd = open(path, OPENFLAG_READ|OPENFLAG_WRITE);
	}
	
	free(iface);
	
	if( fd == -1 ) {
		fprintf(stderr, "Unable to open TCP Client for reading\n");
		return -1;
	}
	
	// Set remote port and address
	printf("Setting port and remote address\n");
	ioctl(fd, 5, &PortNumber);
	ioctl(fd, 6, addrBuffer);
	
	// Connect
	printf("Initiating connection\n");
	if( ioctl(fd, 7, NULL) == 0 ) {
		// Shouldn't happen :(
		fprintf(stderr, "Unable to start connection\n");
		return -1;
	}
	
	// Return descriptor
	return fd;
}

/**
 * \brief Read a space-separated value from a string
 */
char *GetValue(char *Src, int *Ofs)
{
	 int	pos = *Ofs;
	char	*ret = Src + pos;
	char	*end;
	
	if( !Src )	return NULL;
	
	while( *ret == ' ' )	ret ++;
	
	end = strchr(ret, ' ');
	if( end ) {
		*end = '\0';
	}
	else {
		end = ret + strlen(ret) - 1;
	}
	
	end ++ ;
	while( *ret == ' ' )	end ++;
	*Ofs = end - Src;
	
	return ret;
}

static inline int isdigit(int ch)
{
	return '0' <= ch && ch < '9';
}
