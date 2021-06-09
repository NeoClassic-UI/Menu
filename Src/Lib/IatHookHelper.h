// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

struct IatHookData
{
#if defined(_M_AMD64) || defined(_M_IX86)
	unsigned char jump[4]; // jump instruction 0x90, 0x90, 0xFF, 0x25
	DWORD jumpOffs; // jump instruction offset
#elif defined(_M_ARM64)
	unsigned char jump[8]; // LDR <address>, BR
#endif
	void *newProc; // the address of the new proc
	void *oldProc; // the address of the old proc
	IMAGE_THUNK_DATA *thunk; // the IAT thunk
};

void InitializeIatHooks( void );
IatHookData *SetIatHook( HMODULE hPatchedModule, const char *targetModule, const char *targetProc, void *newProc );
void ClearIatHook( IatHookData *hook );
void ClearIatHooks( void );
