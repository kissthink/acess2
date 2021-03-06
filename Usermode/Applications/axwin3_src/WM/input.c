/*
 * Acess2 GUI (AxWin) Version 3
 * - By John Hodge (thePowersGang)
 *
 * input.c
 * - Input abstraction
 */
#include <common.h>
#include <acess/sys.h>
#include <wm_input.h>
#include <stddef.h>

// TODO: Move out to a common header
typedef struct
{
	int Num;
	int Value;
} tNumValue;
#define JOY_IOCTL_GETSETAXISLIMIT	6
#define JOY_IOCTL_GETSETAXISPOSITION	7

// === IMPORTS ===
extern void	Video_SetCursorPos(short X, short Y);
const char	*gsMouseDevice;
extern int	giTerminalFD_Input;
extern int	giScreenWidth;
extern int	giScreenHeight;

// === GLOBALS ===
 int	giMouseFD;
 int	giInput_MouseButtonState;
 int	giInput_MouseX, giInput_MouseY;

// === CODE ===
int Input_Init(void)
{
	tNumValue	num_value;

	// Open mouse for RW
	giMouseFD = _SysOpen(gsMouseDevice, 3);

	// Set mouse limits
	// TODO: Update these if the screen resolution changes
	num_value.Num = 0;	num_value.Value = giScreenWidth;
	_SysIOCtl(giMouseFD, JOY_IOCTL_GETSETAXISLIMIT, &num_value);
	num_value.Value = giScreenWidth/2;
	giInput_MouseX = giScreenWidth/2;
	_SysIOCtl(giMouseFD, JOY_IOCTL_GETSETAXISPOSITION, &num_value);

	num_value.Num = 1;	num_value.Value = giScreenHeight;
	_SysIOCtl(giMouseFD, JOY_IOCTL_GETSETAXISLIMIT, &num_value);
	num_value.Value = giScreenHeight/2;
	giInput_MouseY = giScreenHeight/2;
	_SysIOCtl(giMouseFD, JOY_IOCTL_GETSETAXISPOSITION, &num_value);

	return 0;
}

void Input_FillSelect(int *nfds, fd_set *set)
{
	if(*nfds < giTerminalFD_Input)	*nfds = giTerminalFD_Input;
	if(*nfds < giMouseFD)	*nfds = giMouseFD;
	FD_SET(giTerminalFD_Input, set);
	FD_SET(giMouseFD, set);
}

void Input_HandleSelect(fd_set *set)
{
	if(FD_ISSET(giTerminalFD_Input, set))
	{
		uint32_t	codepoint;
		static uint32_t	scancode;
		#define KEY_CODEPOINT_MASK	0x3FFFFFFF
		
		size_t readlen = _SysRead(giTerminalFD_Input, &codepoint, sizeof(codepoint));
		if( readlen != sizeof(codepoint) )
		{
			// oops, error
			_SysDebug("Terminal read failed? (%i != %i)", readlen, sizeof(codepoint));
		}
	
//		_SysDebug("Keypress 0x%x", codepoint);
	
		switch(codepoint & 0xC0000000)
		{
		case 0x00000000:	// Key pressed
			WM_Input_KeyDown(codepoint & KEY_CODEPOINT_MASK, scancode);
		case 0x80000000:	// Key release
			WM_Input_KeyFire(codepoint & KEY_CODEPOINT_MASK, scancode);
			scancode = 0;
			break;
		case 0x40000000:	// Key refire
			WM_Input_KeyUp(codepoint & KEY_CODEPOINT_MASK, scancode);
			scancode = 0;
			break;
		case 0xC0000000:	// Raw scancode
			scancode = codepoint & KEY_CODEPOINT_MASK;
			break;
		}
	}

	if(FD_ISSET(giMouseFD, set))
	{
		const int c_n_axies = 4;
		const int c_n_buttons = 5;
		 int	i;
		struct sMouseAxis {
			 int16_t	MinValue;
			 int16_t	MaxValue;
			 int16_t	CurValue;
			uint16_t	CursorPos;
		}	*axies;
		uint8_t	*buttons;
		struct sMouseHdr {
			uint16_t	NAxies;
			uint16_t	NButtons;
		}	*mouseinfo;
		char	data[sizeof(*mouseinfo) + sizeof(*axies)*c_n_axies + c_n_buttons];

		mouseinfo = (void*)data;

		_SysSeek(giMouseFD, 0, SEEK_SET);
		i = _SysRead(giMouseFD, data, sizeof(data));
		i -= sizeof(*mouseinfo);
		if( i < 0 ) {
			_SysDebug("Mouse data undersized (no header)");
			return ;
		}
		if( mouseinfo->NAxies > c_n_axies || mouseinfo->NButtons > c_n_buttons ) {
			_SysDebug("%i axies, %i buttons above prealloc counts (%i, %i)",
				mouseinfo->NAxies, mouseinfo->NButtons, c_n_axies, c_n_buttons
				);
			return ;
		}
		if( i < sizeof(*axies)*mouseinfo->NAxies + mouseinfo->NButtons ) {
			_SysDebug("Mouse data undersized (body doesn't fit %i < %i)",
				i, sizeof(*axies)*mouseinfo->NAxies + mouseinfo->NButtons
				);
			return ;
		}

		// What? No X/Y?
		if( mouseinfo->NAxies < 2 )
			return ;
	
		axies = (void*)( mouseinfo + 1 );
		buttons = (void*)( axies + mouseinfo->NAxies );

		// Handle movement
		Video_SetCursorPos( axies[0].CursorPos, axies[1].CursorPos );

//		_SysDebug("Mouse to %i,%i", axies[0].CursorPos, axies[1].CursorPos);

		WM_Input_MouseMoved(
			giInput_MouseX, giInput_MouseY,
			axies[0].CursorPos, axies[1].CursorPos
			);
		giInput_MouseX = axies[0].CursorPos;
		giInput_MouseY = axies[1].CursorPos;

		for( i = 0; i < mouseinfo->NButtons; i ++ )
		{
			 int	bit = 1 << i;
			 int	cur = buttons[i] > 128;

			if( !!(giInput_MouseButtonState & bit) != cur )
			{
				WM_Input_MouseButton(giInput_MouseX, giInput_MouseY, i, cur);
				// Flip button state
				giInput_MouseButtonState ^= bit;
			}
		}
	}
}
