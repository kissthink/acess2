; AcessOS 1
; By thePowersGang
; LD-ACESS.SO
; - helpers.asm

%include "../libacess.so_src/syscalls.inc.asm"

[global _SysDebug]
[global _SysExit]
[global _SysLoadBin]
[global _SysUnloadBin]
[global _SysSetFaultHandler]
[global _open]
[global _close]

; void SysDebugV(char *fmt, va_list Args)
_SysDebug:
	;xchg bx, bx
	push ebp
	mov ebp, esp
	pusha
	
	mov eax, 0x100	; User Debug
	mov ebx, [ebp+8]	; Format
	mov ecx, [ebp+12]	; Arguments
	mov edx, [ebp+16]	; Arguments
	mov edi, [ebp+20]	; Arguments
	mov esi, [ebp+24]	; Arguments
	int	0xAC
	
	popa
	pop ebp
	ret

; void SysExit()
_SysExit:
	push ebx
	mov eax, SYS_EXIT	; Exit
	mov ebx, [esp+0x8]	; Exit Code
	int	0xAC
	pop ebx
	ret

; Uint SysLoadBin(char *path, Uint *entry)
_SysLoadBin:
	push ebx
	mov eax, SYS_LOADBIN	; SYS_LDBIN
	mov ebx, [esp+0x8]	; Path
	mov ecx, [esp+0xC]	; Entry
	int	0xAC
	pop ebx
	ret

; Uint SysUnloadBin(Uint Base)
_SysUnloadBin:
	push ebx
	mov eax, SYS_UNLOADBIN	; SYS_ULDBIN
	mov ebx, [esp+0x8]	; Base
	int	0xAC
	pop ebx
	ret

; int close(char *name, int flags)
_open:
	push ebx
	mov eax, SYS_OPEN
	mov ebx, [esp+0x8]	; Filename
	mov ecx, [esp+0xC]	; Flags
	int 0xAC
	pop ebx
	ret

; void close(int fd)
_close:
	push ebx
	mov eax, SYS_CLOSE
	mov ebx, [esp+0x8]	; File Descriptor
	int 0xAC
	pop ebx
	ret

_SysSetFaultHandler:
	push ebx
	mov eax, SYS_SETFAULTHANDLER
	mov ebx, [esp+0x8]	; File Descriptor
	int 0xAC
	pop ebx
	ret
