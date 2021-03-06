/*
 * Acess2 Window Manager v3
 * - By John Hodge (thePowersGang)
 *
 * render_classes.c
 * - Simple class based window renderer
 */
#include <common.h>
#include <wm_renderer.h>
#include <renderer_classful.h>

// === TYPES ===
typedef struct sClassfulInfo
{
	tColour	BGColour;
} tClassfulInfo;

// === PROTOTYPES ===
tWindow	*Renderer_Class_Create(int Flags);
void	Renderer_Class_Redraw(tWindow *Window);
int	Renderer_Class_HandleMessage(tWindow *Target, int Msg, int Len, void *Data);

// === GLOBALS ===
tWMRenderer	gRenderer_Class = {
	.Name = "Classful",
	.CreateWindow = Renderer_Class_Create,
	.Redraw = Renderer_Class_Redraw,
	.HandleMessage = Renderer_Class_HandleMessage
};

// === CODE ===
int Renderer_Class_Init(void)
{
	WM_RegisterRenderer(&gRenderer_Class);	

	return 0;
}

tWindow	*Renderer_Class_Create(int Flags)
{
	return WM_CreateWindowStruct(sizeof(tClassfulInfo));
}

void Renderer_Class_Redraw(tWindow *Window)
{
	tClassfulInfo	*info = Window->RendererInfo;
	WM_Render_FillRect(Window, 0, 0, Window->W, Window->H, info->BGColour);
}

int Renderer_Class_HandleMessage(tWindow *Target, int Msg, int Len, void *Data)
{
	tClassfulInfo	*info = Target->RendererInfo;
	switch(Msg)
	{
	case MSG_CLASSFUL_SETBGCOLOUR:
		if( Len != sizeof(uint32_t) ) return -1;
		info->BGColour = *(uint32_t*)Data;
		return 0;

	case MSG_CLASSFUL_SETTEXT:
		
		return -1;
	
	// Anything else is unhandled
	default:
		return 1;
	}
}

