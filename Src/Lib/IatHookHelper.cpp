// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include "IatHookHelper.h"
#include "Assert.h"

struct ImgDelayDescr
{
	DWORD grAttrs;      // attributes
	DWORD rvaDLLName;   // RVA to dll name
	DWORD rvaHmod;      // RVA of module handle
	DWORD rvaIAT;       // RVA of the IAT
	DWORD rvaINT;       // RVA of the INT
	DWORD rvaBoundIAT;  // RVA of the optional bound IAT
	DWORD rvaUnloadIAT; // RVA of optional copy of original IAT
	DWORD dwTimeStamp;  // 0 if not bound, O.W. date/time stamp of DLL bound to (Old BIND)
};

static void *PtrFromRva( IMAGE_DOS_HEADER *dosHeader, size_t offset )
{
	return (BYTE*)dosHeader+offset;
}

static IatHookData *g_IatHooks;
static int g_IatHookCount;

const int MAX_IAT_HOOKS=4096/sizeof(IatHookData);

void InitializeIatHooks( void )
{
	Assert(!g_IatHooks);
	g_IatHooks=(IatHookData*)VirtualAlloc(NULL,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE);
	g_IatHookCount=0;
}

void ClearIatHooks( void )
{
	if (!g_IatHooks) return;
	for (int i=0;i<g_IatHookCount;i++)
	{
		if (g_IatHooks[i].jump[0])
			return; // still used
	}
	VirtualFree(g_IatHooks,0,MEM_RELEASE);
	g_IatHooks=NULL;
	g_IatHookCount=0;
}

IatHookData *SetIatHook( IMAGE_DOS_HEADER *dosHeader, DWORD iatOffset, DWORD intOfset, const char *targetProc, void *newProc )
{
	IMAGE_THUNK_DATA *thunk=(IMAGE_THUNK_DATA*)PtrFromRva(dosHeader,iatOffset);
	IMAGE_THUNK_DATA *origThunk=(IMAGE_THUNK_DATA*)PtrFromRva(dosHeader,intOfset);
	for (;origThunk->u1.Function;origThunk++,thunk++)
	{
		if (origThunk->u1.Ordinal&IMAGE_ORDINAL_FLAG)
		{
			if (IS_INTRESOURCE(targetProc) && IMAGE_ORDINAL(origThunk->u1.Ordinal)==(uintptr_t)targetProc)
				break;
		}
		else
		{
			IMAGE_IMPORT_BY_NAME *import=(IMAGE_IMPORT_BY_NAME*)PtrFromRva(dosHeader,origThunk->u1.AddressOfData);
			if (!IS_INTRESOURCE(targetProc) && strcmp(targetProc,(char*)import->Name)==0)
				break;
		}
	}
	if (origThunk->u1.Function)
	{
		IatHookData *hook=g_IatHooks+g_IatHookCount;
		g_IatHookCount++;
#if defined(_M_AMD64) || defined(_M_IX86)
		hook->jump[0]=hook->jump[1]=0x90; // NOP
		hook->jump[2]=0xFF; hook->jump[3]=0x25; // JUMP
#if defined(_M_AMD64)
		hook->jumpOffs=0;
#else
		hook->jumpOffs=(DWORD)(hook)+8;
#endif
#elif defined(_M_ARM64)
		hook->jump[0]=0x48; hook->jump[1]=0x00; hook->jump[2]=0x00; hook->jump[3]=0x58; // LDR X8, newProc
		hook->jump[4]=0x00; hook->jump[5]=0x01; hook->jump[6]=0x1F; hook->jump[7]=0xD6; // BR X8
#endif
		hook->newProc=newProc;
		hook->oldProc=(void*)thunk->u1.Function;
		hook->thunk=thunk;
		DWORD oldProtect;
		VirtualProtect(&thunk->u1.Function,sizeof(void*),PAGE_READWRITE,&oldProtect);
		thunk->u1.Function=(DWORD_PTR)hook;
		VirtualProtect(&thunk->u1.Function,sizeof(void*),oldProtect,&oldProtect);
		return hook;
	}
	return NULL;
}

IatHookData *SetIatHook( HMODULE hPatchedModule, const char *targetModule, const char *targetProc, void *newProc )
{
	ATLASSERT(g_IatHooks);
	if (g_IatHookCount>=MAX_IAT_HOOKS) return NULL;
	IMAGE_DOS_HEADER *dosHeader=(IMAGE_DOS_HEADER*)hPatchedModule;
	IMAGE_NT_HEADERS *ntHeader=(IMAGE_NT_HEADERS*)PtrFromRva(dosHeader,dosHeader->e_lfanew);
	if (ntHeader->Signature!=IMAGE_NT_SIGNATURE) return NULL;

	IMAGE_IMPORT_DESCRIPTOR *importDescriptor=(IMAGE_IMPORT_DESCRIPTOR*)PtrFromRva(dosHeader,ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	for (;importDescriptor->Characteristics!=0;importDescriptor++)
	{
		const char *dllName=(char*)PtrFromRva(dosHeader,importDescriptor->Name);
		if (_stricmp(dllName,targetModule)!=0) continue;

		if (!importDescriptor->FirstThunk || !importDescriptor->OriginalFirstThunk) break;

		return SetIatHook(dosHeader,importDescriptor->FirstThunk,importDescriptor->OriginalFirstThunk,targetProc,newProc);
	}

	ImgDelayDescr *delayDescriptor=(ImgDelayDescr*)PtrFromRva(dosHeader,ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress);

	for (;delayDescriptor->rvaDLLName!=0;delayDescriptor++)
	{
		const char *dllName=(char*)PtrFromRva(dosHeader,delayDescriptor->rvaDLLName);
		if (_stricmp(dllName,targetModule)!=0) continue;

		if (!delayDescriptor->rvaIAT || !delayDescriptor->rvaINT) break;

		return SetIatHook(dosHeader,delayDescriptor->rvaIAT,delayDescriptor->rvaINT,targetProc,newProc);
	}
	return NULL;
}

void ClearIatHook( IatHookData *hook )
{
	if (!hook || !hook->jump[0]) return;
	if (hook->thunk->u1.Function==(DWORD_PTR)hook)
	{
		// the hook was untouched by anybody else
		DWORD oldProtect;
		VirtualProtect(&hook->thunk->u1.Function,sizeof(void*),PAGE_READWRITE,&oldProtect);
		void *cex=InterlockedCompareExchangePointer((void**)&hook->thunk->u1.Function,hook->oldProc,hook);
		VirtualProtect(&hook->thunk->u1.Function,sizeof(void*),oldProtect,&oldProtect);
		if (cex==hook)
		{
			hook->jump[0]=0;
			return; // successfully replaced the original function
		}
	}
	// failed to replace the original function, leave behind the thunk
	hook->newProc=hook->oldProc;
}
