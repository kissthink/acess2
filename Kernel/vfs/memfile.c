/* 
 * Acess 2
 * Virtual File System
 * - Memory Pseudo Files
 */
#include <acess.h>
#include <vfs.h>

// === PROTOTYPES ===
tVFS_Node	*VFS_MemFile_Create(tVFS_Node *Unused, char *Path);
void	VFS_MemFile_Close(tVFS_Node *Node);
Uint64	VFS_MemFile_Read(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer);
Uint64	VFS_MemFile_Write(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer);

// === GLOBALS ===
tVFS_Node	gVFS_MemRoot = {
	.Flags = VFS_FFLAG_DIRECTORY,
	.NumACLs = 0,
	.FindDir = VFS_MemFile_Create
	};

// === CODE ===
/**
 * \fn tVFS_Node *VFS_MemFile_Create(tVFS_Node *Unused, char *Path)
 * \note Treated as finddir by VFS_ParsePath
 */
tVFS_Node *VFS_MemFile_Create(tVFS_Node *Unused, char *Path)
{
	Uint	base, size;
	char	*str = Path;
	tVFS_Node	*ret;
	
	str++;	// Eat '$'
	
	// Read Base address
	base = 0;
	for( ; ('0' <= *str && *str <= '9') || ('A' <= *str && *str <= 'F'); str++ )
	{
		base *= 16;
		if('A' <= *str && *str <= 'F')
			base += *str - 'A' + 10;
		else
			base += *str - '0';
	}
	
	// Check separator
	if(*str++ != ':')	return NULL;
	
	// Read buffer size
	size = 0;
	for( ; ('0' <= *str && *str <= '9') || ('A' <= *str && *str <= 'F'); str++ )
	{
		size *= 16;
		if('A' <= *str && *str <= 'F')
			size += *str - 'A' + 10;
		else
			size += *str - '0';
	}
	
	// Check for NULL byte
	if(*str != '\0') 	return NULL;
	
	Log(" VFS_MemFile_Create: base=0x%x, size=0x%x", base, size);
	
	// Allocate and fill node
	ret = malloc(sizeof(tVFS_Node));
	memset(ret, 0, sizeof(tVFS_Node));
	
	// State
	ret->ImplPtr = (void*)base;
	ret->Size = size;
	
	// ACLs
	ret->NumACLs = 1;
	ret->ACLs = &gVFS_ACL_EveryoneRWX;
	
	// Functions
	ret->Close = VFS_MemFile_Close;
	ret->Read = VFS_MemFile_Read;
	ret->Write = VFS_MemFile_Write;
	
	return ret;
}

/**
 * \fn void VFS_MemFile_Close(tVFS_Node *Node)
 * \brief Dereference and clean up a memory file
 */
void VFS_MemFile_Close(tVFS_Node *Node)
{
	Node->ReferenceCount --;
	if( Node->ReferenceCount == 0 ) {
		Node->ImplPtr = NULL;
		free(Node);
	}
}

/**
 * \fn Uint64 VFS_MemFile_Read(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer)
 * \brief Read from a memory file
 */
Uint64 VFS_MemFile_Read(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer)
{
	// Check for use of free'd file
	if(Node->ImplPtr == NULL)	return 0;
	
	// Check for out of bounds read
	if(Offset > Node->Size)	return 0;
	
	// Truncate data read if needed
	if(Offset + Length > Node->Size)
		Length = Node->Size - Offset;
	
	// Copy Data
	memcpy(Buffer, Node->ImplPtr+Offset, Length);
	
	return Length;
}

/**
 * \fn Uint64 VFS_MemFile_Write(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer)
 * \brief Write to a memory file
 */
Uint64 VFS_MemFile_Write(tVFS_Node *Node, Uint64 Offset, Uint64 Length, void *Buffer)
{
	// Check for use of free'd file
	if(Node->ImplPtr == NULL)	return 0;
	
	// Check for out of bounds read
	if(Offset > Node->Size)	return 0;
	
	// Truncate data read if needed
	if(Offset + Length > Node->Size)
		Length = Node->Size - Offset;
	
	// Copy Data
	memcpy(Node->ImplPtr+Offset, Buffer, Length);
	
	return Length;
}
