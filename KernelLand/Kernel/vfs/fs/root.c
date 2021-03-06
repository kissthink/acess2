/* 
 * Acess2 Kernel
 * - By John Hodge (thePowersGang)
 *
 * vfs/fs/root.c
 * - Root Filesystem Driver
 *
 * TODO: Restrict to directories+symlinks only
 */
#define DEBUG	0
#include <acess.h>
#include <vfs.h>
#include <vfs_ramfs.h>

// === CONSTANTS ===
#define MAX_FILES	64
#define	MAX_FILE_SIZE	10*1024*1024

// === PROTOTYPES ===
tVFS_Node	*Root_InitDevice(const char *Device, const char **Options);
tVFS_Node	*Root_GetByINode(tVFS_Node *RootNode, Uint64 Inode);
tVFS_Node	*Root_MkNod(tVFS_Node *Node, const char *Name, Uint Flags);
tVFS_Node	*Root_FindDir(tVFS_Node *Node, const char *Name, Uint Flags);
 int	Root_ReadDir(tVFS_Node *Node, int Pos, char Dest[FILENAME_MAX]);
size_t	Root_Read(tVFS_Node *Node, off_t Offset, size_t Length, void *Buffer, Uint Flags);
size_t	Root_Write(tVFS_Node *Node, off_t Offset, size_t Length, const void *Buffer, Uint Flags);
tRamFS_File	*Root_int_AllocFile(void);

// === GLOBALS ===
tVFS_Driver	gRootFS_Info = {
	.Name = "rootfs", 
	.InitDevice = Root_InitDevice,
	.GetNodeFromINode = Root_GetByINode
	};
tRamFS_File	RootFS_Files[MAX_FILES];
tVFS_ACL	RootFS_DirACLs[3] = {
	{{0,0}, {0,VFS_PERM_ALL}},	// Owner (Root)
	{{1,0}, {0,VFS_PERM_ALL}},	// Group (Root)
	{{0,-1}, {0,VFS_PERM_ALL^VFS_PERM_WRITE}}	// World (Nobody)
};
tVFS_ACL	RootFS_FileACLs[3] = {
	{{0,0}, {0,VFS_PERM_ALL^VFS_PERM_EXEC}},	// Owner (Root)
	{{1,0}, {0,VFS_PERM_ALL^VFS_PERM_EXEC}},	// Group (Root)
	{{0,-1}, {0,VFS_PERM_READ}}	// World (Nobody)
};
tVFS_NodeType	gRootFS_DirType = {
	.TypeName = "RootFS-Dir",
	.ReadDir = Root_ReadDir,
	.FindDir = Root_FindDir,
	.MkNod = Root_MkNod
};
tVFS_NodeType	gRootFS_FileType = {
	.TypeName = "RootFS-File",
	.Read = Root_Read,
	.Write = Root_Write,
};

// === CODE ===
/**
 * \brief Initialise the root filesystem
 */
tVFS_Node *Root_InitDevice(const char *Device, const char **Options)
{
	tRamFS_File	*root;
	if(strcmp(Device, "root") != 0) {
		return NULL;
	}
	
	// Create Root Node
	root = &RootFS_Files[0];

	root->Name[0] = '/';
	root->Name[1] = '\0';
	root->Node.ImplPtr = root;
	
	root->Node.CTime
		= root->Node.MTime
		= root->Node.ATime = now();
	root->Node.NumACLs = 3;
	root->Node.ACLs = RootFS_DirACLs;

	root->Node.Flags = VFS_FFLAG_DIRECTORY;
	root->Node.Type = &gRootFS_DirType;
	
	return &root->Node;
}

tVFS_Node *Root_GetByINode(tVFS_Node *RootNode, Uint64 Inode)
{
	if( Inode >= MAX_FILES )
		return NULL;
	if( RootFS_Files[Inode].Name[0] == '\0' )
		return NULL;
	Debug("Root_GetByINode: (%llx) = '%s' %p",
		Inode,
		RootFS_Files[Inode].Name, &RootFS_Files[Inode].Node
		);
	return &RootFS_Files[Inode].Node;
}

/**
 * \fn int Root_MkNod(tVFS_Node *Node, const char *Name, Uint Flags)
 * \brief Create an entry in the root directory
 */
tVFS_Node *Root_MkNod(tVFS_Node *Node, const char *Name, Uint Flags)
{
	tRamFS_File	*parent = Node->ImplPtr;
	tRamFS_File	*child;
	tRamFS_File	*prev = NULL;
	
	ENTER("pNode sName xFlags", Node, Name, Flags);
	
	LOG("Sanity check name length - %i > %i", strlen(Name)+1, sizeof(child->Name));
	if(strlen(Name) + 1 > sizeof(child->Name)) {
		errno = EINVAL;
		LEAVE_RET('n', NULL);
	}
	
	// Find last child, while we're at it, check for duplication
	for( child = parent->Data.FirstChild; child; prev = child, child = child->Next )
	{
		if(strcmp(child->Name, Name) == 0) {
			LOG("Duplicate");
			errno = EEXIST;
			LEAVE_RET('n', NULL);
		}
	}
	
	child = Root_int_AllocFile();
	
	strcpy(child->Name, Name);
	LOG("Name = '%s'", child->Name);
	
	child->Parent = parent;
	child->Next = NULL;
	child->Data.FirstChild = NULL;
	
	child->Node.ImplPtr = child;
	child->Node.Flags = Flags;
	child->Node.NumACLs = 3;
	child->Node.Size = 0;
	
	if(Flags & VFS_FFLAG_DIRECTORY)
	{
		child->Node.ACLs = RootFS_DirACLs;
		child->Node.Type = &gRootFS_DirType;
	} else {
		if(Flags & VFS_FFLAG_SYMLINK)
			child->Node.ACLs = RootFS_DirACLs;
		else
			child->Node.ACLs = RootFS_FileACLs;
		child->Node.Type = &gRootFS_FileType;
	}
	
	// Append!
	if( prev )
		prev->Next = child;
	else
		parent->Data.FirstChild = child;
	
	parent->Node.Size ++;
	
	LEAVE('n', &child->Node);
	return &child->Node;
}

/**
 * \fn tVFS_Node *Root_FindDir(tVFS_Node *Node, const char *Name)
 * \brief Find an entry in the filesystem
 */
tVFS_Node *Root_FindDir(tVFS_Node *Node, const char *Name, Uint Flags)
{
	tRamFS_File	*parent = Node->ImplPtr;
	tRamFS_File	*child = parent->Data.FirstChild;
	
	ENTER("pNode sName", Node, Name);
	
	for( child = parent->Data.FirstChild; child; child = child->Next )
	{
		LOG("child->Name = '%s'", child->Name);
		if(strcmp(child->Name, Name) == 0)
		{
			LEAVE('p', &child->Node);
			return &child->Node;
		}
	}
	
	LEAVE('n');
	return NULL;
}

/**
 * \fn char *Root_ReadDir(tVFS_Node *Node, int Pos)
 * \brief Get an entry from the filesystem
 */
int Root_ReadDir(tVFS_Node *Node, int Pos, char Dest[FILENAME_MAX])
{
	tRamFS_File	*parent = Node->ImplPtr;
	tRamFS_File	*child = parent->Data.FirstChild;
	
	for( ; child && Pos--; child = child->Next ) ;
	
	if(child) {
		strncpy(Dest, child->Name, FILENAME_MAX);
		return 0;
	}
	
	return -ENOENT;
}

/**
 * \brief Read from a file in the root directory
 */
size_t Root_Read(tVFS_Node *Node, off_t Offset, size_t Length, void *Buffer, Uint Flags)
{
	tRamFS_File	*file = Node->ImplPtr;
	
	if(Offset > Node->Size)	return 0;

	if(Length > Node->Size)
		Length = Node->Size;
	if(Offset+Length > Node->Size)
		Length = Node->Size - Offset;
	
	memcpy(Buffer, file->Data.Bytes+Offset, Length);
	
	return Length;
}

/**
 * \brief Write to a file in the root directory
 */
size_t Root_Write(tVFS_Node *Node, off_t Offset, size_t Length, const void *Buffer, Uint Flags)
{
	tRamFS_File	*file = Node->ImplPtr;

	ENTER("pNode XOffset xLength pBuffer", Node, Offset, Length, Buffer);

	if(Offset > Node->Size) {
		LEAVE('i', -1);
		return -1;
	}	

	if(Offset + Length > MAX_FILE_SIZE)
	{
		Length = MAX_FILE_SIZE - Offset;
		ASSERTC(Length, <=, MAX_FILE_SIZE);
	}

	LOG("Length = %x", Length);
	
	// Check if buffer needs to be expanded
	if(Offset + Length > Node->Size)
	{
		size_t	newsize = Offset + Length;
		void *tmp = realloc( file->Data.Bytes, newsize );
		if(tmp == NULL)	{
			Warning("Root_Write - Increasing buffer size failed (0x%x)",
				newsize);
			LEAVE('i', -1);
			return -1;
		}
		file->Data.Bytes = tmp;
		Node->Size = Offset + Length;
		LOG("Expanded buffer to %i bytes", (int)Node->Size);
	}
	
	memcpy(file->Data.Bytes+Offset, Buffer, Length);
	LOG("File - '%.*s'", Node->Size, file->Data.Bytes);
	
	LEAVE('i', Length);
	return Length;
}

/**
 * \fn tRamFS_File *Root_int_AllocFile(void)
 * \brief Allocates a file from the pool
 */
tRamFS_File *Root_int_AllocFile(void)
{
	 int	i;
	for( i = 0; i < MAX_FILES; i ++ )
	{
		if( RootFS_Files[i].Name[0] == '\0' )
		{
			RootFS_Files[i].Node.Inode = i;
			return &RootFS_Files[i];
		}
	}
	return NULL;
}
