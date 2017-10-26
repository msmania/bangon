#define KDEXT_64BIT

#include <windows.h>
#include "wdbgexts.h"
#include "bangon.h"

CPEImage::CPEImage(ULONG64 ImageBase) : mImageBase(0), mPlatform(mPlatform) {
    ZeroMemory(&mVersion, sizeof(mVersion));
    ZeroMemory(&mResourceDir, sizeof(mResourceDir));
    ZeroMemory(&mImportDir, sizeof(mImportDir));
    Initialize(ImageBase);
}

CPEImage::~CPEImage() {
}

bool CPEImage::IsInitialized() const {
    return (mImageBase!=0);
}

bool CPEImage::Is64bit() const {
    return (mPlatform==IMAGE_FILE_MACHINE_AMD64 || mPlatform==IMAGE_FILE_MACHINE_IA64);
}

WORD CPEImage::GetPlatform() const {
    return mPlatform;
}

void CPEImage::GetVersion(PDWORD FileVersionMS,
                PDWORD FileVersionLS,
                PDWORD ProductVersionMS,
                PDWORD ProductVersionLS) const {
    if ( FileVersionMS ) *FileVersionMS = mVersion.Value.dwFileVersionMS;
    if ( FileVersionLS ) *FileVersionLS = mVersion.Value.dwFileVersionLS;
    if ( ProductVersionMS ) *ProductVersionMS = mVersion.Value.dwProductVersionMS;
    if ( ProductVersionLS ) *ProductVersionLS = mVersion.Value.dwProductVersionLS;
}

bool CPEImage::LoadVersion() {
    bool Ret = false;
    ULONG Status = 0;
    ULONG64 ll = 0;
    ULONG BytesRead = 0;

    CHAR buf1[20];

    ULONG i;
    IMAGE_RESOURCE_DIRECTORY ResDirectory;
    IMAGE_RESOURCE_DIRECTORY_ENTRY ResDirEntry;
    IMAGE_RESOURCE_DATA_ENTRY DataEntry;
    VS_VERSIONINFO VersionInfo;

    if ( !IsInitialized() ) return false;

    ll = mImageBase + mResourceDir.VirtualAddress;
    Status = ReadMemory(ll, &ResDirectory, sizeof(ResDirectory), &BytesRead);
    if ( !Status || BytesRead!=sizeof(ResDirectory) ) {
        dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    //
    // Searching for VS_FILE_INFO directory entry
    //
    ZeroMemory(&ResDirEntry, sizeof(ResDirEntry));
    ll += sizeof(ResDirectory) + ResDirectory.NumberOfNamedEntries * sizeof(ResDirEntry);
    for ( i=0 ; i<ResDirectory.NumberOfIdEntries ; ++i ) {
        Status = ReadMemory(ll, &ResDirEntry, sizeof(ResDirEntry), &BytesRead);
        if ( !Status || BytesRead!=sizeof(ResDirEntry) ) {
            dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY_ENTRY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            goto exit;
        }

        if ( !ResDirEntry.NameIsString && MAKEINTRESOURCE(ResDirEntry.Id)==VS_FILE_INFO ) {
            break;
        }

        ll += sizeof(ResDirEntry);
    }

    if ( i>=(ULONG)ResDirectory.NumberOfIdEntries || !ResDirEntry.DataIsDirectory) {
        dprintf("VS_FILE_INFO resource not found. Failed to determine version.\n");
        goto exit;
    }

    // Getting directory from entry
    ll = mImageBase + mResourceDir.VirtualAddress + ResDirEntry.OffsetToDirectory;
    Status = ReadMemory(ll, &ResDirectory, sizeof(ResDirectory), &BytesRead);
    if ( !Status || BytesRead!=sizeof(ResDirectory) ) {
        dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    //
    // Searching for VS_VERSION_INFO directory entry
    //
    ll += sizeof(ResDirectory) + ResDirectory.NumberOfNamedEntries * sizeof(ResDirEntry);
    for ( i=0 ; i<ResDirectory.NumberOfIdEntries ; ++i ) {
        Status = ReadMemory(ll, &ResDirEntry, sizeof(ResDirEntry), &BytesRead);
        if ( !Status || BytesRead!=sizeof(ResDirEntry) ) {
            dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY_ENTRY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            goto exit;
        }

        if ( !ResDirEntry.NameIsString && ResDirEntry.Id==VS_VERSION_INFO ) {
            break;
        }

        ll += sizeof(ResDirEntry);
    }

    if ( i>=(ULONG)ResDirectory.NumberOfIdEntries || !ResDirEntry.DataIsDirectory) {
        dprintf("VS_VERSION_INFO resource not found. Failed to determine version.\n");
        goto exit;
    }

    // Getting directory from entry
    ll = mImageBase + mResourceDir.VirtualAddress + ResDirEntry.OffsetToDirectory;
    Status = ReadMemory(ll, &ResDirectory, sizeof(ResDirectory), &BytesRead);
    if ( !Status || BytesRead!=sizeof(ResDirectory) ) {
        dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    // Make sure directory has an entry
    if ( ResDirectory.NumberOfIdEntries<1 ) {
        dprintf("VS_VERSION_INFO has not entry.\n");
        goto exit;
    }

    // Getting the first entry from directory
    ll += sizeof(ResDirectory);
    Status = ReadMemory(ll, &ResDirEntry, sizeof(ResDirEntry), &BytesRead);
    if ( !Status || BytesRead!=sizeof(ResDirEntry) ) {
        dprintf("Failed to read IMAGE_RESOURCE_DIRECTORY_ENTRY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    // Make sure VS_VERSION_INFO is not a directory
    if ( ResDirEntry.DataIsDirectory ) {
        dprintf("VS_VERSION_INFO is not a data entry.\n");
        goto exit;
    }

    // Getting data entry from entry
    ll = mImageBase + mResourceDir.VirtualAddress + ResDirEntry.OffsetToDirectory;
    Status = ReadMemory(ll, &DataEntry, sizeof(DataEntry), &BytesRead);
    if ( !Status || BytesRead!=sizeof(DataEntry) ) {
        dprintf("Failed to read IMAGE_RESOURCE_DATA_ENTRY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    if ( DataEntry.Size<sizeof(VS_VERSIONINFO) ) {
        dprintf("VS_VERSION_INFO buffer is short.\n");
        goto exit;
    }

    ll = mImageBase + DataEntry.OffsetToData;
    Status = ReadMemory(ll, &VersionInfo, sizeof(VersionInfo), &BytesRead);
    if ( !Status || BytesRead!=sizeof(VersionInfo) ) {
        dprintf("Failed to read VS_VERSION_INFO at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms646997(v=vs.85).aspx
    if ( VersionInfo.Value.dwSignature!=0xFEEF04BD ) {
        dprintf("VS_VERSION_INFO signature does not match.\n");
        goto exit;
    }

    CopyMemory(&mVersion, &VersionInfo, sizeof(mVersion));
    Ret = true;

exit:
    return Ret;
}

// https://msdn.microsoft.com/en-us/magazine/cc301808.aspx
bool CPEImage::Initialize(ULONG64 ImageBase) {
    CONST DWORD OFFSET_PEHEADER = 0x3c; // _IMAGE_DOS_HEADER::e_lfanew
    CONST DWORD PE_SIGNATURE = 0x4550;

    bool Ret = false;
    ULONG Status = 0;
    ULONG64 ll = 0;
    ULONG BytesRead = 0;

    DWORD Rva_PEHeader = 0;
    DWORD PESignature = 0;

    CHAR buf1[20];

    WORD Platform = 0;
    IMAGE_DATA_DIRECTORY DataDirectory;

    ll = ImageBase + OFFSET_PEHEADER;
    Status = ReadMemory(ll, &Rva_PEHeader, sizeof(Rva_PEHeader), &BytesRead);
    if ( !Status || BytesRead!=sizeof(Rva_PEHeader) ) {
        dprintf("Failed to access DOS header at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    ll = ImageBase + Rva_PEHeader;
    Status = ReadMemory(ll, &PESignature, sizeof(PESignature), &BytesRead);
    if ( !Status || BytesRead!=sizeof(PESignature) || PESignature!=PE_SIGNATURE ) {
        dprintf("PE header not found at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    ll = ImageBase + Rva_PEHeader + 4; // ntdll!_IMAGE_NT_HEADERS::FileHeader::Machine
    Status = ReadMemory(ll, &Platform, sizeof(Platform), &BytesRead);
    if ( !Status || BytesRead!=sizeof(Platform) ) {
        dprintf("Failed to access PE header at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }

    if ( Platform!=IMAGE_FILE_MACHINE_AMD64 &&
         Platform!=IMAGE_FILE_MACHINE_IA64 &&
         Platform!=IMAGE_FILE_MACHINE_I386 ) {
        dprintf("Unsupported platform - 0x%04x. Initialization failed.\n", Platform);
        goto exit;
    }

    mImageBase = ImageBase;
    mPlatform = Platform;

    //
    // Getting RVA to each directory
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms680305(v=vs.85).aspx
    //
    ll = ImageBase + Rva_PEHeader + 0x18 + (Is64bit() ? 0x70 : 0x60); // pointing to the first entry

    // Import table: 2nd entry
    ll += sizeof(DataDirectory);
    Status = ReadMemory(ll, &DataDirectory, sizeof(DataDirectory), &BytesRead);
    if ( !Status || BytesRead!=sizeof(DataDirectory) ) {
        dprintf("Failed to read IMAGE_DATA_DIRECTORY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }
    CopyMemory(&mImportDir, &DataDirectory, sizeof(DataDirectory));

    // Resource table: 3rd entry
    ll += sizeof(DataDirectory);
    Status = ReadMemory(ll, &DataDirectory, sizeof(DataDirectory), &BytesRead);
    if ( !Status || BytesRead!=sizeof(DataDirectory) ) {
        dprintf("Failed to read IMAGE_DATA_DIRECTORY at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
        goto exit;
    }
    CopyMemory(&mResourceDir, &DataDirectory, sizeof(DataDirectory));

    Ret = true;

exit:
    return Ret;
}

ULONG CPEImage::ReadPointerEx(ULONG64 Address, PULONG64 Pointer) const {
    ULONG cb = 0;
    if ( Is64bit() ) {
        return (ReadMemory(Address, (PVOID)Pointer, sizeof(*Pointer), &cb) &&
                cb == sizeof(*Pointer));
    }
    else {
        ULONG Pointer32;
        ULONG Status;
        Status = ReadMemory(Address,
                            (PVOID)&Pointer32,
                            sizeof(Pointer32),
                            &cb);
        if (Status && cb == sizeof(Pointer32)) {
            *Pointer = (ULONG64)(LONG64)(LONG)Pointer32;
            return 1;
        }
        return 0;
    }
}

void CPEImage::DumpAddressTable(LPCSTR DllName, const IMAGE_IMPORT_DESCRIPTOR &ImportDesc) const {
    ULONG Status = 0;
    ULONG64 ll = 0;
    DWORD BytesRead = 0;

    ULONG64 DataBuffer = 0;
    DWORD Ordinal = 0;
    ULONG64 Displacement = 0;

    CHAR buf1[20];

    CONST INT MAX_NAMEBUFFER = 100;
    CHAR NameBuffer[MAX_NAMEBUFFER];
    CHAR SymbolBuffer[MAX_NAMEBUFFER];

    CONST DWORD AddressSize = Is64bit() ? 8 : 4;

    for ( int i = 0 ;; ++i ) {
        ll = mImageBase + ImportDesc.Characteristics + i * AddressSize;
        if ( !ReadPointerEx(ll, &DataBuffer) ) {
            dprintf("Failed to read the name table entry at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        if (!DataBuffer) break; // detecting the last sentinel

        ll = mImageBase + DataBuffer;
        Status = ReadMemory(ll, &Ordinal, sizeof(WORD), &BytesRead);
        if ( !Status || BytesRead!=sizeof(WORD) ) {
            dprintf("Failed to read the ordinal at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        ll = mImageBase + DataBuffer + 2;
        Status = ReadMemory(ll, &NameBuffer, sizeof(NameBuffer), &BytesRead);
        if ( !Status || BytesRead!=sizeof(NameBuffer) ) {
            dprintf("Failed to read the function name at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        ll = mImageBase + ImportDesc.FirstThunk + i * AddressSize;
        if ( !ReadPointerEx(ll, &DataBuffer) ) {
            dprintf("Failed to read the function address at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        Displacement = 0;
        GetSymbol(DataBuffer, SymbolBuffer, &Displacement);

        dprintf("%4d %10s@%04x %-32s %s %s+0x%x\n", i,
            DllName,
            Ordinal,
            NameBuffer,
            ptos(DataBuffer, buf1, sizeof(buf1)),
            SymbolBuffer, Displacement);
    }
}

#define DUMP_ALL ((LPCSTR)1)

void CPEImage::DumpImportTable(LPCSTR DllName) const {
    ULONG Status = 0;
    ULONG64 ll = 0;
    DWORD BytesRead = 0;

    CHAR buf1[20];

    CONST INT MAX_NAMEBUFFER = 100;
    CHAR NameBuffer[MAX_NAMEBUFFER];

    if ( !IsInitialized() ) return;

    for ( DWORD i = 0 ; i<mImportDir.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR) ; ++i ) {
        ULONG64 ImportDescAddress = 0;
        IMAGE_IMPORT_DESCRIPTOR ImportDesc;

        ll = ImportDescAddress = mImageBase + mImportDir.VirtualAddress + i * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        Status = ReadMemory(ll, &ImportDesc, sizeof(ImportDesc), &BytesRead);
        if ( !Status || BytesRead!=sizeof(ImportDesc) ) {
            dprintf("Failed to read IMAGE_IMPORT_DESCRIPTOR at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        if (!ImportDesc.Name) break; // detecting the last sentinel

        ZeroMemory(NameBuffer, sizeof(NameBuffer));

        ll = mImageBase + ImportDesc.Name;
        Status = ReadMemory(ll, NameBuffer, sizeof(NameBuffer), &BytesRead);
        if ( !Status || BytesRead!=MAX_NAMEBUFFER ) {
            dprintf("Failed to read the module name at 0x%s\n", ptos(ll, buf1, sizeof(buf1)));
            break;
        }

        if ( DllName==0 ) {
            dprintf("%4d %s %s\n", i, ptos(ImportDescAddress, buf1, sizeof(buf1)), NameBuffer);
        }
        else if ( DllName==DUMP_ALL ) {
            DumpAddressTable(NameBuffer, ImportDesc);
        }
        else if ( _stricmp(NameBuffer, DllName)==0 ) {
            DumpAddressTable(NameBuffer, ImportDesc);
            break;
        }
    }
}

DECLARE_API(imp) {
    const char Delim[] = " ";
    char Args[1024];
    char *Token = NULL;
    char *NextToken = NULL;

    if ( args && strcpy_s(Args, sizeof(Args), args)==0 ) {
        Token = strtok_s(Args, Delim, &NextToken);
        if ( !Token ) return;

        CPEImage pe(GetExpression(Token));

        Token = strtok_s(NULL, Delim, &NextToken);

        if ( pe.IsInitialized() ) {
            pe.DumpImportTable(Token && _stricmp(Token, "-ALL")==0 ? DUMP_ALL : Token);
        }
    }
}
