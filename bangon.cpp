//
// bangon.cpp
//

// IsPtr64() is supported only in 64bit pointer mode.
#define KDEXT_64BIT

#include <Windows.h>
#include <set>
#include <queue>

#include "wdbgexts.h"

#define LODWORD(ll) ((DWORD)((ll)&0xffffffff))
#define HIDWORD(ll) ((DWORD)(((ll)>>32)&0xffffffff))

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

DECLARE_API (help) {
    dprintf("!dt <RTL_SPLAY_LINKS*>                         - dump splay tree\n\n");
}

struct TREE_ITEM32 {
    ULONG Parent;
    ULONG LeftChild;
    ULONG RightChild;
};

struct TREE_ITEM64 {
    ULONG64 Parent;
    ULONG64 LeftChild;
    ULONG64 RightChild;
};

struct TREE_ITEM_INFO {
    DWORD Level;
    ULONG64 Myself;
    union {
        TREE_ITEM32 Item32;
        TREE_ITEM64 Item64;
    } u;

    ULONG64 Parent() const {
        return IsPtr64() ? u.Item64.Parent : u.Item32.Parent;
    }

    ULONG64 LeftChild() const {
        return IsPtr64() ? u.Item64.LeftChild : u.Item32.LeftChild;
    }

    ULONG64 RightChild() const {
        return IsPtr64() ? u.Item64.RightChild : u.Item32.RightChild;
    }
};

std::queue<TREE_ITEM_INFO> TraverseQueue;
std::set<ULONG64> CorruptionCheck;

static LPCSTR ptos(ULONG64 p, LPSTR s, ULONG len) {
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

BOOL AddTreeItem(DWORD Level, ULONG64 Address) {
    if ( CorruptionCheck.find(Address)!=CorruptionCheck.end() ) {
        return FALSE;
    }

    CorruptionCheck.insert(Address);

    ULONG Status = 0;
    DWORD BytesRead = 0;
    DWORD BytesToRead = 0;

    TREE_ITEM_INFO TreeItem;
    TreeItem.Level = Level;
    TreeItem.Myself = Address;
    BytesToRead = IsPtr64() ? sizeof(TREE_ITEM64) : sizeof(TREE_ITEM32);
    Status = ReadMemory(Address, &(TreeItem.u), BytesToRead, &BytesRead);
    if ( !Status || BytesRead!=BytesToRead ) {
        return FALSE;
    }

    TraverseQueue.push(TreeItem);
    return TRUE;
}

DECLARE_API (dt) {
    ULONG64 RootAddress = 0;
    CHAR buf1[20];
    CHAR buf2[20];
    CHAR buf3[20];
    CHAR buf4[20];

    RootAddress = GetExpression(args);
    if ( !RootAddress )
        return;

    if ( !IsPtr64() ) {
        // preventing sign extension
        RootAddress &= 0x0ffffffff;
    }

    while ( TraverseQueue.size() ) {
        TraverseQueue.pop();
    }
    CorruptionCheck.clear();

    AddTreeItem(0, RootAddress);

    DWORD CurrentLevel = 0;
    DWORD ItemCount = 0;
    while ( TraverseQueue.size() ) {
        const TREE_ITEM_INFO &Item = TraverseQueue.front();

        dprintf("L=%04x#%04x %s : P= %s L= %s R= %s\n",
            CurrentLevel, ItemCount,
            ptos(Item.Myself, buf1, sizeof(buf1)),
            ptos(Item.Parent(), buf2, sizeof(buf2)),
            ptos(Item.LeftChild(), buf3, sizeof(buf3)),
            ptos(Item.RightChild(), buf4, sizeof(buf4)));
        
        if ( Item.Level!=CurrentLevel ) {
            ItemCount = 0;
            CurrentLevel = Item.Level;
        }
        
        if ( Item.LeftChild() ) {
            if ( !AddTreeItem(Item.Level+1, Item.LeftChild()) ) {
                dprintf("Item %s was duplicated!\n",
                    ptos(Item.LeftChild(), buf1, sizeof(buf1)));
            }
        }

        if ( Item.RightChild() ) {
            if ( !AddTreeItem(Item.Level+1, Item.RightChild()) ) {
                dprintf("Item %s was duplicated!\n",
                    ptos(Item.RightChild(), buf1, sizeof(buf1)));
            }
        }

        ++ItemCount;
        TraverseQueue.pop();
    }
}
