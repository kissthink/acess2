/*
 * Acess 2 USB Stack
 * - By John Hodge (thePowersGang)
 * 
 * usb_lowlevel.c
 * - Low Level IO
 */
#define DEBUG	1
#include <acess.h>
#include "usb.h"
#include "usb_proto.h"
#include "usb_lowlevel.h"
#include <timers.h>

// === PROTOTYPES ===
void	*USB_int_Request(tUSBHost *Host, int Addr, int EndPt, int Type, int Req, int Val, int Indx, int Len, void *Data);
 int	USB_int_SendSetupSetAddress(tUSBHost *Host, int Address);
 int	USB_int_ReadDescriptor(tUSBDevice *Dev, int Endpoint, int Type, int Index, int Length, void *Dest);
char	*USB_int_GetDeviceString(tUSBDevice *Dev, int Endpoint, int Index);
 int	_UTF16to8(Uint16 *Input, int InputLen, char *Dest);

// === CODE ===
void *USB_int_Request(tUSBHost *Host, int Addr, int EndPt, int Type, int Req, int Val, int Indx, int Len, void *Data)
{
	void	*hdl;
	// TODO: Sanity check (and check that Type is valid)
	struct sDeviceRequest	req;
	 int	dest = Addr * 16 + EndPt;	// TODO: Validate
	
	ENTER("pHost xdest iType iReq iVal iIndx iLen pData",
		Host, dest, Type, Req, Val, Indx, Len, Data);
	
	req.ReqType = Type;
	req.Request = Req;
	req.Value = LittleEndian16( Val );
	req.Index = LittleEndian16( Indx );
	req.Length = LittleEndian16( Len );

	LOG("SETUP");	
	hdl = Host->HostDef->SendSETUP(Host->Ptr, dest, 0, NULL, NULL, &req, sizeof(req));

	// TODO: Data toggle?
	// TODO: Multi-packet transfers
	if( Type & 0x80 )
	{
		void	*hdl2;
		
		LOG("IN");
		hdl = Host->HostDef->SendIN(Host->Ptr, dest, 0, NULL, NULL, Data, Len);

		LOG("OUT (Done)");
		hdl2 = Host->HostDef->SendOUT(Host->Ptr, dest, 0, INVLPTR, NULL, NULL, 0);
		LOG("Wait...");
		while( Host->HostDef->IsOpComplete(Host->Ptr, hdl2) == 0 )
			Time_Delay(1);
	}
	else
	{
		void	*hdl2;
		
		LOG("OUT");
		if( Len > 0 )
			hdl = Host->HostDef->SendOUT(Host->Ptr, dest, 0, NULL, NULL, Data, Len);
		else
			hdl = NULL;
		
		LOG("IN (Status)");
		// Status phase (DataToggle=1)
		hdl2 = Host->HostDef->SendIN(Host->Ptr, dest, 1, INVLPTR, NULL, NULL, 0);
		LOG("Wait...");
		while( Host->HostDef->IsOpComplete(Host->Ptr, hdl2) == 0 )
			Time_Delay(1);
	}
	LEAVE('p', hdl);
	return hdl;
}

int USB_int_SendSetupSetAddress(tUSBHost *Host, int Address)
{
	USB_int_Request(Host, 0, 0, 0x00, 5, Address & 0x7F, 0, 0, NULL);
	return 0;
}

int USB_int_ReadDescriptor(tUSBDevice *Dev, int Endpoint, int Type, int Index, int Length, void *Dest)
{
	const int	ciMaxPacketSize = 0x400;
	struct sDeviceRequest	req;
	 int	bToggle = 0;
	void	*final;
	 int	dest = Dev->Address*16 + Endpoint;

	ENTER("pDev xdest iType iIndex iLength pDest",
		Dev, dest, Type, Index, Length, Dest);

	req.ReqType = 0x80;
	req.ReqType |= ((Type >> 8) & 0x3) << 5;	// Bits 5/6
	req.ReqType |= (Type >> 12) & 3;	// Destination (Device, Interface, Endpoint, Other);

	req.Request = 6;	// GET_DESCRIPTOR
	req.Value = LittleEndian16( ((Type & 0xFF) << 8) | (Index & 0xFF) );
	req.Index = LittleEndian16( 0 );	// TODO: Language ID / Interface
	req.Length = LittleEndian16( Length );

	LOG("SETUP");	
	Dev->Host->HostDef->SendSETUP(
		Dev->Host->Ptr, dest,
		0, NULL, NULL,
		&req, sizeof(req)
		);
	
	bToggle = 1;
	while( Length > ciMaxPacketSize )
	{
		LOG("IN (%i rem)", Length - ciMaxPacketSize);
		Dev->Host->HostDef->SendIN(
			Dev->Host->Ptr, dest,
			bToggle, NULL, NULL,
			Dest, ciMaxPacketSize
			);
		bToggle = !bToggle;
		Length -= ciMaxPacketSize;
	}

	LOG("IN (final)");
	final = Dev->Host->HostDef->SendIN(
		Dev->Host->Ptr, dest,
		bToggle, INVLPTR, NULL,
		Dest, Length
		);

	LOG("Waiting");
	while( Dev->Host->HostDef->IsOpComplete(Dev->Host->Ptr, final) == 0 )
		Threads_Yield();	// BAD BAD BAD

	LEAVE('i', 0);
	return 0;
}

char *USB_int_GetDeviceString(tUSBDevice *Dev, int Endpoint, int Index)
{
	struct sDescriptor_String	str;
	 int	src_len, new_len;
	char	*ret;

	if(Index == 0)	return strdup("");
	
	USB_int_ReadDescriptor(Dev, Endpoint, 3, Index, sizeof(str), &str);
	if(str.Length < 2) {
		Log_Error("USB", "String %p:%i:%i:%i descriptor is undersized (%i)",
			Dev->Host, Dev->Address, Endpoint, Index, str.Length);
		return NULL;
	}
//	if(str.Length > sizeof(str)) {
//		// IMPOSSIBLE!
//		Log_Error("USB", "String is %i bytes, which is over prealloc size (%i)",
//			str.Length, sizeof(str)
//			);
//	}
	src_len = (str.Length - 2) / sizeof(str.Data[0]);

	LOG("&str = %p, src_len = %i", &str, src_len);

	new_len = _UTF16to8(str.Data, src_len, NULL);	
	ret = malloc( new_len + 1 );
	_UTF16to8(str.Data, src_len, ret);
	ret[new_len] = 0;
	return ret;
}

int _UTF16to8(Uint16 *Input, int InputLen, char *Dest)
{
	 int	str_len, cp_len;
	Uint32	saved_bits = 0;
	str_len = 0;
	for( int i = 0; i < InputLen; i ++)
	{
		Uint32	cp;
		Uint16	val = Input[i];
		if( val >= 0xD800 && val <= 0xDBFF )
		{
			// Multibyte - Leading
			if(i + 1 > InputLen) {
				cp = '?';
			}
			else {
				saved_bits = (val - 0xD800) << 10;
				saved_bits += 0x10000;
				continue ;
			}
		}
		else if( val >= 0xDC00 && val <= 0xDFFF )
		{
			if( !saved_bits ) {
				cp = '?';
			}
			else {
				saved_bits |= (val - 0xDC00);
				cp = saved_bits;
			}
		}
		else
			cp = val;

		cp_len = WriteUTF8((Uint8*)Dest, cp);
		if(Dest)
			Dest += cp_len;
		str_len += cp_len;

		saved_bits = 0;
	}
	
	return str_len;
}

