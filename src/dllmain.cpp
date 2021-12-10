#define KDEXT_64BIT
#include <windows.h>
#include <wdbgexts.h>

BOOL WINAPI DllMain(_In_ HINSTANCE, _In_ DWORD reason, _In_ LPVOID) {
  switch (reason) {
  case DLL_PROCESS_ATTACH:
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
  case DLL_PROCESS_DETACH:
    break;
  }
  return TRUE;
}

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff543968(v=vs.85).aspx
EXT_API_VERSION ApiVersion = {
  0, // MajorVersion
  0, // MinorVersion
  EXT_API_VERSION_NUMBER64, // Revision
  0 // Reserved
};

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff561303(v=vs.85).aspx
// ExtensionApis is extern defined as WINDBG_EXTENSION_APIS in wdbgexts.h
WINDBG_EXTENSION_APIS ExtensionApis;

LPEXT_API_VERSION ExtensionApiVersion(void) {
  return &ApiVersion;
}

void init_target_info();

VOID WinDbgExtensionDllInit(PWINDBG_EXTENSION_APIS lpExtensionApis,
                            USHORT MajorVersion,
                            USHORT MinorVersion) {
  ExtensionApis = *lpExtensionApis;
  init_target_info();
}

DECLARE_API(help) {
  dprintf(
    "!cfg <ImageBase>                   - dump GuardCFFunctionTable\n"
    "!dt  <RTL_SPLAY_LINKS*>            - dump splay tree\n"
    "!delay <Imagebase>                 - dump delayload import table\n"
    "!ex  <Imagebase> [<Code Address>]  - display SEH info\n"
    "!ext <Imagebase>                   - display export table\n"
    "!imp <Imagebase> [* | <Module>]    - display import table\n"
    "!sec <Imagebase>                   - display section table\n"
    "!v2p <VirtAddr> [<DirBase>] [32]   - paging translation\n"
    "!ver <Imagebase>                   - display version info\n"
    "\n");
}
