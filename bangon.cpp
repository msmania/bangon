//
// bangon.cpp
//

#define KDEXT_64BIT

#include <Windows.h>
#include <stdio.h>
#include "wdbgexts.h"
#include "bangon.h"

//
// http://msdn.microsoft.com/en-us/library/windows/hardware/ff543968(v=vs.85).aspx
//
EXT_API_VERSION ApiVersion = {
    0,	// MajorVersion
    0,	// MinorVersion
    EXT_API_VERSION_NUMBER64,	// Revision
    0	// Reserved
};

//
// http://msdn.microsoft.com/en-us/library/windows/hardware/ff561303(v=vs.85).aspx
// ExtensionApis is extern defined as WINDBG_EXTENSION_APIS in wdbgexts.h
//
WINDBG_EXTENSION_APIS ExtensionApis;

LPEXT_API_VERSION ExtensionApiVersion(void) {
    return &ApiVersion;
}

VOID WinDbgExtensionDllInit(
  PWINDBG_EXTENSION_APIS lpExtensionApis,
  USHORT MajorVersion,
  USHORT MinorVersion
) {
    ExtensionApis = *lpExtensionApis;
    return;
}

LPCSTR ptos(ULONG64 p, LPSTR s, ULONG len) {
    LPCSTR Ret = NULL;
    if ( HIDWORD(p)==0 && len>=9 ) {
        sprintf_s(s, len, "%08x", LODWORD(p));
        Ret = s;
    }
    else if ( HIDWORD(p)>0 && len>=18 ) {
        sprintf_s(s, len, "%08x`%08x", HIDWORD(p), LODWORD(p));
        Ret = s;
    }
    return Ret;
}

DECLARE_API (help) {
    dprintf("!dt <RTL_SPLAY_LINKS*>                         - dump splay tree\n");
    dprintf("!dt <Imagebase address>                        - display version info\n\n");
}

DECLARE_API (ver) {
    CHAR buf1[20];
    ULONG64 ImageBase = GetExpression(args);
    DWORD FileMS = 0;
    DWORD FileLS = 0;
    DWORD ProdMS = 0;
    DWORD ProdLS = 0;

    CPEImage pe(ImageBase);
    if ( pe.IsInitialized() ) {
        pe.GetVersion(&FileMS, &FileLS, &ProdMS, &ProdLS);
        dprintf("ImageBase:      %s\n", ptos(ImageBase, buf1, sizeof(buf1)));
        dprintf("Platform:       %04x\n", pe.GetPlatform());
        dprintf("FileVersion:    %08x.%08x\n", FileMS, FileLS);
        dprintf("ProductVersion: %08x.%08x\n", ProdMS, ProdLS);
    }
}
