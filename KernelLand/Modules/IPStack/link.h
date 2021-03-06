/*
 * Acess2 IP Stack
 * - Link/Media Layer Header
 */
#ifndef _LINK_H_
#define _LINK_H_

#include "include/buffer.h"

// === EXTERNAL ===
typedef void (*tPacketCallback)(tAdapter *Interface, tMacAddr From, int Length, void *Buffer);

extern void	Link_RegisterType(Uint16 Type, tPacketCallback Callback);
extern void	Link_SendPacket(tAdapter *Interface, Uint16 Type, tMacAddr To, tIPStackBuffer *Buffer);
extern int	Link_HandlePacket(tAdapter *Adapter, tIPStackBuffer *Buffer);
extern void	Link_WatchDevice(tAdapter *Adapter);

// === INTERNAL ===
typedef struct sEthernetHeader	tEthernetHeader;
typedef struct sEthernetFooter	tEthernetFooter;
struct sEthernetHeader {
	tMacAddr	Dest;
	tMacAddr	Src;
	Uint16	Type;
	Uint8	Data[];
};

struct sEthernetFooter {
	//Uint32	CRC;
};

#endif
