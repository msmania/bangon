#define LODWORD(ll) ((DWORD)((ll)&0xffffffff))
#define HIDWORD(ll) ((DWORD)(((ll)>>32)&0xffffffff))

LPCSTR ptos(ULONG64 p, LPSTR s, ULONG len);

class CPEImage {
private:
    ULONG64 mImageBase;
    WORD mPlatform;
    IMAGE_DATA_DIRECTORY mResourceDir;
    IMAGE_DATA_DIRECTORY mImportDir;

    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms647001(v=vs.85).aspx
    struct VS_VERSIONINFO {
        WORD wLength;
        WORD wValueLength;
        WORD wType;
        WCHAR szKey[16]; // L"VS_VERSION_INFO"
        VS_FIXEDFILEINFO Value;
    } mVersion;

    // We cannot use ReadPointer() because IsPtr64() returns true in a WOW64 process.
    ULONG ReadPointerEx(ULONG64 Address, PULONG64 Pointer) const;

    bool Initialize(ULONG64 ImageBase);
    void DumpAddressTable(LPCSTR DllName, const IMAGE_IMPORT_DESCRIPTOR &ImportDesc) const;

public:
    CPEImage(ULONG64 ImageBase);
    virtual ~CPEImage();

    bool IsInitialized() const;
    bool Is64bit() const;

    bool LoadVersion();

    WORD GetPlatform() const;
    void GetVersion(PDWORD FileVersionMS,
                    PDWORD FileVersionLS,
                    PDWORD ProductVersionMS,
                    PDWORD ProductVersionLS) const;

    void DumpImportTable(LPCSTR DllName) const;
};