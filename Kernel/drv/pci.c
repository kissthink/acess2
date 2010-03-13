/*
 * AcessOS/AcessBasic v0.1
 * PCI Bus Driver
 */
#define DEBUG	0
#include <acess.h>
#include <modules.h>
#include <vfs.h>
#include <fs_devfs.h>
#include <drv_pci.h>

#define	LIST_DEVICES	1

// === STRUCTURES ===
typedef struct s_pciDevice {
	Uint16	bus, slot, fcn;
	Uint16	vendor, device;
	union {
		struct {Uint8 class, subclass;};
		Uint16	oc;
	};
	Uint16	revision;
	Uint32	ConfigCache[256/4];
	char	Name[8];
	tVFS_Node	Node;
} t_pciDevice;

// === CONSTANTS ===
#define SPACE_STEP	5
#define MAX_RESERVED_PORT	0xD00

// === PROTOTYPES ===
 int	PCI_Install(char **Arguments);
char	*PCI_ReadDirRoot(tVFS_Node *node, int pos);
tVFS_Node	*PCI_FindDirRoot(tVFS_Node *node, char *filename);
Uint64	PCI_ReadDevice(tVFS_Node *node, Uint64 pos, Uint64 length, void *buffer);
 
 int	PCI_CountDevices(Uint16 vendor, Uint16 device, Uint16 fcn);
 int	PCI_GetDevice(Uint16 vendor, Uint16 device, Uint16 fcn, int idx);
 int	PCI_GetDeviceByClass(Uint16 class, Uint16 mask, int prev);
Uint8	PCI_GetIRQ(int id);
Uint32	PCI_GetBAR0(int id);
Uint32	PCI_GetBAR1(int id);
Uint32	PCI_GetBAR3(int id);
Uint32	PCI_GetBAR4(int id);
Uint32	PCI_GetBAR5(int id);
Uint16	PCI_AssignPort(int id, int bar, int count);

 int	PCI_EnumDevice(Uint16 bus, Uint16 dev, Uint16 fcn, t_pciDevice *info);
Uint32	PCI_CfgReadDWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset);
void	PCI_CfgWriteDWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset, Uint32 data);
Uint16	PCI_CfgReadWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset);
Uint8	PCI_CfgReadByte(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset);

// === GLOBALS ===
//MODULE_DEFINE(0, 0x0100, PCI, PCI_Install, NULL);
 int	giPCI_BusCount = 1;
 int	giPCI_InodeHandle = -1;
 int	giPCI_DeviceCount = 0;
t_pciDevice	*gPCI_Devices = NULL;
tDevFS_Driver	gPCI_DriverStruct = {
	NULL, "pci",
	{
	.Flags = VFS_FFLAG_DIRECTORY,
	.Size = -1,
	.NumACLs = 1,
	.ACLs = &gVFS_ACL_EveryoneRX,
	.ReadDir = PCI_ReadDirRoot,
	.FindDir = PCI_FindDirRoot
	}
};
 Uint32	*gaPCI_PortBitmap = NULL;
 
// === CODE ===
/**
 * \fn int PCI_Install()
 * \brief Scan the PCI Bus for devices
 */
int PCI_Install(char **Arguments)
{
	 int	bus, dev, fcn, i;
	 int	space = 0;
	t_pciDevice	devInfo;
	void	*tmpPtr = NULL;
	
	// Build Portmap
	gaPCI_PortBitmap = malloc( 1 << 13 );
	memset( gaPCI_PortBitmap, 0, 1 << 13 );
	for( i = 0; i < MAX_RESERVED_PORT / 32; i ++ )
		gaPCI_PortBitmap[i] = -1;
	for( i = 0; i < MAX_RESERVED_PORT % 32; i ++ )
		gaPCI_PortBitmap[MAX_RESERVED_PORT / 32] = 1 << i;
	
	// Scan Busses
	for( bus = 0; bus < giPCI_BusCount; bus++ )
	{
		for( dev = 0; dev < 10; dev++ )	// 10 Devices per bus
		{
			for( fcn = 0; fcn < 8; fcn++ )	// 8 functions per device
			{
				// Check if the device/function exists
				if(!PCI_EnumDevice(bus, dev, fcn, &devInfo))
				{
					continue;
				}
				
				if(giPCI_DeviceCount == space)
				{
					space += SPACE_STEP;
					tmpPtr = realloc(gPCI_Devices, space*sizeof(t_pciDevice));
					if(tmpPtr == NULL)
						break;
					gPCI_Devices = tmpPtr;
				}
				if(devInfo.oc == PCI_OC_PCIBRIDGE)
				{
					#if LIST_DEVICES
					Log("[PCI ] Bridge @ %i,%i:%i (0x%x:0x%x)",
						bus, dev, fcn, devInfo.vendor, devInfo.device);
					#endif
					giPCI_BusCount++;
				}
				devInfo.Node.Inode = giPCI_DeviceCount;
				memcpy(&gPCI_Devices[giPCI_DeviceCount], &devInfo, sizeof(t_pciDevice));
				giPCI_DeviceCount ++;
				#if LIST_DEVICES
				Log("[PCI ] Device %i,%i:%i => 0x%x:0x%x",
					bus, dev, fcn, devInfo.vendor, devInfo.device);
				#endif
				
				// WTF is this for?
				if(fcn == 0) {
					if( !(devInfo.ConfigCache[3] & 0x800000) )
						break;
				}
			}
			if(tmpPtr != gPCI_Devices)
				break;
		}
		if(tmpPtr != gPCI_Devices)
			break;
	}
	
	if(giPCI_DeviceCount == 0)
		return MODULE_ERR_NOTNEEDED;
	
	tmpPtr = realloc(gPCI_Devices, giPCI_DeviceCount*sizeof(t_pciDevice));
	if(tmpPtr == NULL)
		return MODULE_ERR_MALLOC;
	gPCI_Devices = tmpPtr;
	
	// Complete Driver Structure	
	gPCI_DriverStruct.RootNode.Size = giPCI_DeviceCount;
	
	// And add to DevFS
	DevFS_AddDevice(&gPCI_DriverStruct);
	
	return MODULE_ERR_OK;
}

/**
 * \fn char *PCI_ReadDirRoot(tVFS_Node *Node, int Pos)
 * \brief Read from Root of PCI Driver
*/
char *PCI_ReadDirRoot(tVFS_Node *Node, int Pos)
{
	ENTER("pNode iPos", Node, Pos);
	if(Pos < 0 || Pos >= giPCI_DeviceCount) {
		LEAVE('n');
		return NULL;
	}
	
	LEAVE('s', gPCI_Devices[Pos].Name);
	return strdup( gPCI_Devices[Pos].Name );
}
/**
 * \fn tVFS_Node *PCI_FindDirRoot(tVFS_Node *node, char *filename)
 */
tVFS_Node *PCI_FindDirRoot(tVFS_Node *node, char *filename)
{
	 int	bus,slot,fcn;
	 int	i;
	// Validate Filename (Pointer and length)
	if(!filename || strlen(filename) != 7)
		return NULL;
	// Check for spacers
	if(filename[2] != '.' || filename[5] != ':')
		return NULL;
	
	// Get Information
	if(filename[0] < '0' || filename[0] > '9')	return NULL;
	bus = (filename[0] - '0')*10;
	if(filename[1] < '0' || filename[1] > '9')	return NULL;
	bus += filename[1] - '0';
	if(filename[3] < '0' || filename[3] > '9')	return NULL;
	slot = (filename[3] - '0')*10;
	if(filename[4] < '0' || filename[4] > '9')	return NULL;
	slot += filename[4] - '0';
	if(filename[6] < '0' || filename[6] > '9')	return NULL;
	fcn = filename[6] - '0';
	
	// Find Match
	for(i=0;i<giPCI_DeviceCount;i++)
	{
		if(gPCI_Devices[i].bus != bus)		continue;
		if(gPCI_Devices[i].slot != slot)	continue;
		if(gPCI_Devices[i].fcn != fcn)	continue;
		
		return &gPCI_Devices[i].Node;
	}
	
	// Error Return
	return NULL;
}

/**
 * \fn Uint64 PCI_ReadDevice(tVFS_Node *node, Uint64 pos, Uint64 length, void *buffer)
 */
Uint64 PCI_ReadDevice(tVFS_Node *node, Uint64 pos, Uint64 length, void *buffer)
{	
	if( pos + length > 256 )	return 0;
	
	memcpy(
		buffer,
		(char*)gPCI_Devices[node->Inode].ConfigCache + pos,
		length);
	
	return length;
}

// --- Kernel Code Interface ---
/**
 \fn int PCI_CountDevices(Uint16 vendor, Uint16 device, Uint16 fcn)
 \brief Counts the devices with the specified codes
 \param vendor	Vendor ID
 \param device	Device ID
 \param fcn	Function ID
*/
int PCI_CountDevices(Uint16 vendor, Uint16 device, Uint16 fcn)
{
	int i, ret=0;
	for(i=0;i<giPCI_DeviceCount;i++)
	{
		if(gPCI_Devices[i].vendor != vendor)	continue;
		if(gPCI_Devices[i].device != device)	continue;
		if(gPCI_Devices[i].fcn != fcn)	continue;
		ret ++;
	}
	return ret;
}

/**
 \fn int PCI_GetDevice(Uint16 vendor, Uint16 device, Uint16 fcn, int idx)
 \brief Gets the ID of the specified PCI device
 \param vendor	Vendor ID
 \param device	Device ID
 \param fcn	Function IDs
 \param idx	Number of matching entry wanted
*/
int PCI_GetDevice(Uint16 vendor, Uint16 device, Uint16 fcn, int idx)
{
	int i, j=0;
	for(i=0;i<giPCI_DeviceCount;i++)
	{
		if(gPCI_Devices[i].vendor != vendor)	continue;
		if(gPCI_Devices[i].device != device)	continue;
		if(gPCI_Devices[i].fcn != fcn)	continue;
		if(j == idx)	return i;
		j ++;
	}
	return -1;
}

/**
 * \fn int PCI_GetDeviceByClass(Uint16 class, Uint16 mask, int prev)
 * \brief Gets the ID of a device by it's class code
 * \param class	Class Code
 * \param mask	Mask for class comparison
 * \param prev	ID of previous device (-1 for no previous)
 */
int PCI_GetDeviceByClass(Uint16 class, Uint16 mask, int prev)
{
	 int	i;
	// Check if prev is negative (meaning get first)
	if(prev < 0)	i = 0;
	else	i = prev+1;
	
	for( ; i < giPCI_DeviceCount; i++ )
	{
		if((gPCI_Devices[i].oc & mask) != class)	continue;
		return i;
	}
	return -1;
}

/**
 \fn Uint8 PCI_GetIRQ(int id)
*/
Uint8 PCI_GetIRQ(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[15];
	//return PCI_CfgReadByte( gPCI_Devices[id].bus, gPCI_Devices[id].slot, gPCI_Devices[id].fcn, 0x3C);
}

/**
 \fn Uint32 PCI_GetBAR0(int id)
*/
Uint32 PCI_GetBAR0(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[4];
}

/**
 \fn Uint32 PCI_GetBAR1(int id)
*/
Uint32 PCI_GetBAR1(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[5];
}

/**
 \fn Uint32 PCI_GetBAR2(int id)
*/
Uint32 PCI_GetBAR2(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[6];
}

/**
 \fn Uint32 PCI_GetBAR3(int id)
*/
Uint32 PCI_GetBAR3(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[7];
}

/**
 \fn Uint32 PCI_GetBAR4(int id)
*/
Uint32 PCI_GetBAR4(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[8];
}

/**
 \fn Uint32 PCI_GetBAR5(int id)
*/
Uint32 PCI_GetBAR5(int id)
{
	if(id < 0 || id >= giPCI_DeviceCount)
		return 0;
	return gPCI_Devices[id].ConfigCache[9];
}

Uint16 PCI_AssignPort(int id, int bar, int count)
{
	Uint16	portVals;
	 int	gran=0;
	 int	i, j;
	t_pciDevice	*dev;
	
	//LogF("PCI_AssignPort: (id=%i,bar=%i,count=%i)\n", id, bar, count);
	
	if(id < 0 || id >= giPCI_DeviceCount)	return 0;
	if(bar < 0 || bar > 5)	return 0;
	
	dev = &gPCI_Devices[id];
	
	PCI_CfgWriteDWord( dev->bus, dev->slot,	dev->fcn, 0x10+bar*4, -1 );
	portVals = PCI_CfgReadDWord( dev->bus, dev->slot, dev->fcn, 0x10+bar*4 );
	dev->ConfigCache[4+bar] = portVals;
	//LogF(" PCI_AssignPort: portVals = 0x%x\n", portVals);
	
	// Check for IO port
	if( !(portVals & 1) )	return 0;
	
	// Mask out final bit
	portVals &= ~1;
	
	// Get Granuality
	__asm__ __volatile__ ("bsf %%eax, %%ecx" : "=c" (gran) : "a" (portVals) );
	gran = 1 << gran;
	//LogF(" PCI_AssignPort: gran = 0x%x\n", gran);
	
	// Find free space
	portVals = 0;
	for( i = 0; i < 1<<16; i += gran )
	{
		for( j = 0; j < count; j ++ )
		{
			if( gaPCI_PortBitmap[ (i+j)>>5 ] & 1 << ((i+j)&0x1F) )
				break;
		}
		if(j == count) {
			portVals = i;
			break;
		}
	}
	
	if(portVals)
	{
		for( j = 0; j < count; j ++ )
		{
			if( gaPCI_PortBitmap[ (portVals+j)>>5 ] |= 1 << ((portVals+j)&0x1F) )
				break;
		}
		PCI_CfgWriteDWord( dev->bus, dev->slot, dev->fcn, 0x10+bar*4, portVals|1 );
		dev->ConfigCache[4+bar] = portVals|1;
	}
	
	// Return
	//LogF("PCI_AssignPort: RETURN 0x%x\n", portVals);
	return portVals;
}

/**
 * \fn int	PCI_EnumDevice(Uint16 bus, Uint16 slot, Uint16 fcn, t_pciDevice *info)
 */
int	PCI_EnumDevice(Uint16 bus, Uint16 slot, Uint16 fcn, t_pciDevice *info)
{
	Uint16	vendor;
	 int	i;
	Uint32	addr;
	
	vendor = PCI_CfgReadWord(bus, slot, fcn, 0x0|0);
	if(vendor == 0xFFFF)	// Invalid Device
		return 0;
		
	info->bus = bus;
	info->slot = slot;
	info->fcn = fcn;
	info->vendor = vendor;
	info->device = PCI_CfgReadWord(bus, slot, fcn, 0x0|2);
	info->revision = PCI_CfgReadWord(bus, slot, fcn, 0x8|0);
	info->oc = PCI_CfgReadWord(bus, slot, fcn, 0x8|2);
	
	// Load Config Bytes
	addr = 0x80000000 | ((Uint)bus<<16) | ((Uint)slot<<11) | ((Uint)fcn<<8);
	for(i=0;i<256/4;i++)
	{
		#if 1
		outd(0xCF8, addr);
		info->ConfigCache[i] = ind(0xCFC);
		addr += 4;
		#else
		info->ConfigCache[i] = PCI_CfgReadDWord(bus, slot, fcn, i*4);
		#endif
	}
	
	//#if LIST_DEVICES
	//Log("BAR0 0x%08x BAR1 0x%08x BAR2 0x%08x", info->ConfigCache[4], info->ConfigCache[5], info->ConfigCache[6]);
	//Log("BAR3 0x%08x BAR4 0x%08x BAR5 0x%08x", info->ConfigCache[7], info->ConfigCache[8], info->ConfigCache[9]);
	//Log("Class: 0x%04x", info->oc);
	//#endif
	
	// Make node name
	info->Name[0] = '0' + bus/10;
	info->Name[1] = '0' + bus%10;
	info->Name[2] = '.';
	info->Name[3] = '0' + slot/10;
	info->Name[4] = '0' + slot%10;
	info->Name[5] = ':';
	info->Name[6] = '0' + fcn;
	info->Name[7] = '\0';
	
	// Create VFS Node
	memset( &info->Node, 0, sizeof(tVFS_Node) );
	info->Node.Size = 256;
	
	info->Node.NumACLs = 1;
	info->Node.ACLs = &gVFS_ACL_EveryoneRO;
	
	info->Node.Read = PCI_ReadDevice;
	
	return 1;
}

Uint32 PCI_CfgReadDWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset)
{
	Uint32	address;
	Uint32	data;
	
	bus &= 0xFF;	// 8 Bits
	dev &= 0x1F;	// 5 Bits
	func &= 0x7;	// 3 Bits
	offset &= 0xFF;	// 8 Bits
	
	address = 0x80000000 | ((Uint)bus<<16) | ((Uint)dev<<11) | ((Uint)func<<8) | (offset&0xFC);
	outd(0xCF8, address);
	
	data = ind(0xCFC);
	return (Uint32)data;
}
void PCI_CfgWriteDWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset, Uint32 data)
{
	Uint32	address;
	
	bus &= 0xFF;	// 8 Bits
	dev &= 0x1F;	// 5 Bits
	func &= 0x7;	// 3 Bits
	offset &= 0xFF;	// 8 Bits
	
	address = 0x80000000 | ((Uint)bus<<16) | ((Uint)dev<<11) | ((Uint)func<<8) | (offset&0xFC);
	outd(0xCF8, address);
	outd(0xCFC, data);
}
Uint16 PCI_CfgReadWord(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset)
{
	Uint32	data;
	
	bus &= 0xFF;	// 8 Bits
	dev &= 0x1F;	// 5 Bits
	func &= 0x7;	// 3 Bits
	offset &= 0xFF;	// 8 Bits
	
	//LogF("PCI_CfgReadWord: (bus=0x%x,dev=0x%x,func=%x,offset=0x%x)\n", bus, dev, func, offset);
	
	outd(0xCF8,
		0x80000000 | ((Uint)bus<<16) | ((Uint)dev<<11) | ((Uint)func<<8) | (offset&0xFC) );
	
	data = ind(0xCFC);
	data >>= (offset&2)*8;	//Allow Access to Upper Word
	//LogF("PCI_CfgReadWord: RETURN 0x%x\n", data&0xFFFF);
	return (Uint16)data;
}

Uint8 PCI_CfgReadByte(Uint16 bus, Uint16 dev, Uint16 func, Uint16 offset)
{
	Uint32	address;
	Uint32	data;
	
	bus &= 0xFF;	// 8 Bits
	dev &= 0x1F;	// 4 Bits
	func &= 0x7;	// 3 Bits
	offset &= 0xFF;	// 8 Bits
	
	address = 0x80000000 | ((Uint)bus<<16) | ((Uint)dev<<11) | ((Uint)func<<8) | (offset&0xFC);
	outd(0xCF8, address);
	
	data = ind(0xCFC);
	data >>= (offset&3)*8;	//Allow Access to Upper Word
	return (Uint8)data;
}


// === EXPORTS ===
//*
EXPORT(PCI_CountDevices);
EXPORT(PCI_GetDevice);
EXPORT(PCI_GetDeviceByClass);
EXPORT(PCI_AssignPort);
EXPORT(PCI_GetIRQ);
//*/
