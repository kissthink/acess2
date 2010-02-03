/*
 * Acess GUI (AxWin) Version 2
 * By John Hodge (thePowersGang)
 */
#include "common.h"
#include <acess/sys.h>
#include <acess/devices/terminal.h>

// === PROTOTYPES ===
void	Video_Setup();
void	Video_Update();

// === GLOBALS ===

// === CODE ===
void Video_Setup()
{
	 int	tmpInt;
	
	// Open terminal
	giTerminalFD = open(gsTerminalDevice, OPENFLAG_READ|OPENFLAG_WRITE);
	if( giTerminalFD == -1 )
	{
		fprintf(stderr, "ERROR: Unable to open '%s' (%i)\n", gsTerminalDevice, _errno);
		exit(-1);
	}
	
	// Set width
	tmpInt = giScreenWidth;
	tmpInt = ioctl( giTerminalFD, TERM_IOCTL_WIDTH, &tmpInt );
	if(tmpInt != giScreenWidth)
	{
		fprintf(stderr, "Warning: Selected width (%i) is invalid, clipped to %i\n",
			giScreenWidth, tmpInt);
		giScreenWidth = tmpInt;
	}
	
	// Set height
	tmpInt = giScreenHeight;
	tmpInt = ioctl( giTerminalFD, TERM_IOCTL_HEIGHT, &tmpInt );
	if(tmpInt != giScreenHeight)
	{
		fprintf(stderr, "Warning: Selected height (%i) is invalid, clipped to %i\n",
			giScreenHeight, tmpInt);
		giScreenHeight = tmpInt;
	}
	
	// Set mode to video
	tmpInt = TERM_MODE_FB;
	ioctl( giTerminalFD, TERM_IOCTL_MODETYPE, &tmpInt );
	
	// Force VT8 to be shown
	ioctl( giTerminalFD, TERM_IOCTL_FORCESHOW, NULL );
	
	// Create local framebuffer (back buffer)
	gpScreenBuffer = malloc( giScreenWidth*giScreenHeight*4 );
	memset32( gpScreenBuffer, 0x8888FF, giScreenWidth*giScreenHeight );
	Video_Update();
}

void Video_Update()
{
	seek(giTerminalFD, 0, SEEK_SET);
	write(giTerminalFD, giScreenWidth*giScreenHeight*4, gpScreenBuffer);
}