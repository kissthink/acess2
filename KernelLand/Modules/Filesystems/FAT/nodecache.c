/*
 * Acess2 FAT12/16/32 Driver
 * - By John Hodge (thePowersGang)
 *
 * nodecache.c
 * - FAT-Specific node caching
 */
#include <acess.h>
#include <vfs.h>
#include "common.h"

// === PROTOTYPES ===
extern tVFS_Node	*FAT_int_CacheNode(tFAT_VolInfo *Disk, const tVFS_Node *Node);

// === CODE ===
/**
 * \brief Creates a tVFS_Node structure for a given file entry
 * \param Parent	Parent directory VFS node
 * \param Entry	File table entry for the new node
 */
tVFS_Node *FAT_int_CreateNode(tVFS_Node *Parent, fat_filetable *Entry)
{
	tVFS_Node	node;
	tVFS_Node	*ret;
	tFAT_VolInfo	*disk = Parent->ImplPtr;

	ENTER("pParent pEntry", Parent, Entry);
	LOG("disk = %p", disk);
	
	if( (ret = FAT_int_GetNode(disk, Entry->cluster | (Entry->clusterHi<<16))) ) {
		LEAVE('p', ret);
		return ret;
	}

	memset(&node, 0, sizeof(tVFS_Node));
	
	// Set Other Data
	// 0-27: Cluster, 32-59: Parent Cluster
	node.Inode = Entry->cluster | (Entry->clusterHi<<16) | (Parent->Inode << 32);
	LOG("node.Inode = %llx", node.Inode);
	node.ImplInt = 0;
	// Disk Pointer
	node.ImplPtr = disk;
	node.Size = Entry->size;
	LOG("Entry->size = %i", Entry->size);
	// root:root
	node.UID = 0;	node.GID = 0;
	node.NumACLs = 1;
	
	node.Flags = 0;
	if(Entry->attrib & ATTR_DIRECTORY)	node.Flags |= VFS_FFLAG_DIRECTORY;
	if(Entry->attrib & ATTR_READONLY) {
		node.Flags |= VFS_FFLAG_READONLY;
		node.ACLs = &gVFS_ACL_EveryoneRX;	// R-XR-XR-X
	}
	else {
		node.ACLs = &gVFS_ACL_EveryoneRWX;	// RWXRWXRWX
	}
	
	// Create timestamps
	node.ATime = timestamp(0,0,0,
			((Entry->adate&0x1F) - 1),	// Days
			((Entry->adate&0x1E0) - 1),	// Months
			1980+((Entry->adate&0xFF00)>>8)	// Years
			);
	
	node.CTime = Entry->ctimems * 10;	// Miliseconds
	node.CTime += timestamp(
			((Entry->ctime&0x1F)<<1),	// Seconds
			((Entry->ctime&0x3F0)>>5),	// Minutes
			((Entry->ctime&0xF800)>>11),	// Hours
			((Entry->cdate&0x1F)-1),		// Days
			((Entry->cdate&0x1E0)-1),		// Months
			1980+((Entry->cdate&0xFF00)>>8)	// Years
			);
			
	node.MTime = timestamp(
			((Entry->mtime&0x1F)<<1),	// Seconds
			((Entry->mtime&0x3F0)>>5),	// Minutes
			((Entry->mtime&0xF800)>>11),	// Hours
			((Entry->mdate&0x1F)-1),		// Days
			((Entry->mdate&0x1E0)-1),		// Months
			1980+((Entry->mdate&0xFF00)>>8)	// Years
			);
	
	// Set pointers
	if(node.Flags & VFS_FFLAG_DIRECTORY) {
		//Log_Debug("FAT", "Directory %08x has size 0x%x", node.Inode, node.Size);
		node.Type = &gFAT_DirType;	
		node.Size = -1;
	}
	else {
		node.Type = &gFAT_FileType;
	}

	// TODO: Cache node	
	ret = FAT_int_CacheNode(disk, &node);
	LEAVE('p', ret);
	return ret;
}

tVFS_Node *FAT_int_CreateIncompleteDirNode(tFAT_VolInfo *Disk, Uint32 Cluster)
{
	// If the directory isn't in the cache, what do?
	// - we want to lock it such that we don't collide, but don't want to put crap data in the cache
	// - Put a temp node in with a flag that indicates it's incomplete?
	return NULL;
}

tVFS_Node *FAT_int_GetNode(tFAT_VolInfo *Disk, Uint32 Cluster)
{
	if( Cluster == Disk->rootOffset )
		return &Disk->rootNode;
	Mutex_Acquire(&Disk->lNodeCache);
	tFAT_CachedNode	*cnode;

	for(cnode = Disk->NodeCache; cnode; cnode = cnode->Next)
	{
		if( (cnode->Node.Inode & 0xFFFFFFFF) == Cluster ) {
			cnode->Node.ReferenceCount ++;
			Mutex_Release(&Disk->lNodeCache);
			return &cnode->Node;
		}
	}	

	Mutex_Release(&Disk->lNodeCache);
	return NULL;
}

tVFS_Node *FAT_int_CacheNode(tFAT_VolInfo *Disk, const tVFS_Node *Node)
{
	tFAT_CachedNode	*cnode, *prev = NULL;
	Mutex_Acquire(&Disk->lNodeCache);
	
	for(cnode = Disk->NodeCache; cnode; prev = cnode, cnode = cnode->Next )
	{
		if( cnode->Node.Inode == Node->Inode ) {
			cnode->Node.ReferenceCount ++;
			Mutex_Release(&Disk->lNodeCache);
			return &cnode->Node;
		}
	}
	
	cnode = malloc(sizeof(tFAT_CachedNode));
	cnode->Next = NULL;
	memcpy(&cnode->Node, Node, sizeof(tVFS_Node));
	cnode->Node.ReferenceCount = 1;
	
	if( prev )
		prev->Next = cnode;
	else
		Disk->NodeCache = cnode;
	
	Mutex_Release(&Disk->lNodeCache);
	return &cnode->Node;
}

void FAT_int_DerefNode(tVFS_Node *Node)
{
	tFAT_VolInfo	*Disk = Node->ImplPtr;
	tFAT_CachedNode	*cnode, *prev = NULL;

	if( Node == &Disk->rootNode )
		return ;	

	Mutex_Acquire(&Disk->lNodeCache);
	Node->ReferenceCount --;
	for(cnode = Disk->NodeCache; cnode; prev = cnode, cnode = cnode->Next )
	{
		if(Node == &cnode->Node) {
			if(prev)
				prev->Next = cnode->Next;
			else
				Disk->NodeCache = cnode->Next;
			break;
		}
	}
	Mutex_Release(&Disk->lNodeCache);
	if( !cnode ) {
		// Not here?
		return ;
	}
	
	// Already out of the list :)
	free(cnode->Node.Data);
	free(cnode);
}

void FAT_int_ClearNodeCache(tFAT_VolInfo *Disk)
{
	// TODO: In theory when this is called, all handles will be closed
}
