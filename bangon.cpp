//
// bangon.cpp
//

#include <Windows.h>
#include <set>
#include <queue>

#include "wdbgexts.h"

#define LODWORD(ll) ((DWORD)(ll&0xffffffff))
#define HIDWORD(ll) ((DWORD)((ll>>32)&0xffffffff))

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
    dprintf("Hello!\n");
}

struct TREE_ITEM64 {
    ULONGLONG Parent;
    ULONGLONG LeftChild;
    ULONGLONG RightChild;
};

struct TREE_ITEM_INFO {
    DWORD Level;
    ULONGLONG Myself;
    TREE_ITEM64 Item;
};

std::queue<TREE_ITEM_INFO> TraverseQueue;
std::set<ULONGLONG> CorruptionCheck;

BOOL AddTreeItem(DWORD Level, ULONGLONG Address) {
    if ( CorruptionCheck.find(Address)!=CorruptionCheck.end() ) {
        return FALSE;
    }

    CorruptionCheck.insert(Address);

    DWORD BytesRead = 0;
    TREE_ITEM_INFO TreeItem;
    TreeItem.Level = Level;
    TreeItem.Myself = Address;
    ReadMemory(Address, &(TreeItem.Item), sizeof(TREE_ITEM64), &BytesRead);

    if ( BytesRead!=sizeof(TREE_ITEM64) ) {
        return FALSE;
    }

    TraverseQueue.push(TreeItem);
    return TRUE;
}

DECLARE_API (dumptree) {
    ULONGLONG RootAddress = GetExpression(args);

    if ( !RootAddress )
        return;
    
    while ( TraverseQueue.size() ) {
        TraverseQueue.pop();
    }
    CorruptionCheck.clear();

    AddTreeItem(0, RootAddress);

    DWORD CurrentLevel = 0;
    DWORD ItemCount = 0;
    while ( TraverseQueue.size() ) {
        const TREE_ITEM_INFO &Item = TraverseQueue.front();

        dprintf("L=%04x#%04x %08x`%08x : P= %08x`%08x L= %08x`%08x R= %08x`%08x\n",
            CurrentLevel, ItemCount,
            HIDWORD(Item.Myself), LODWORD(Item.Myself),
            HIDWORD(Item.Item.Parent), LODWORD(Item.Item.Parent),
            HIDWORD(Item.Item.LeftChild), LODWORD(Item.Item.LeftChild),
            HIDWORD(Item.Item.RightChild), LODWORD(Item.Item.RightChild));
        
        if ( Item.Level!=CurrentLevel ) {
            ItemCount = 0;
            CurrentLevel = Item.Level;
        }
        
        if ( Item.Item.LeftChild ) {
            if ( !AddTreeItem(Item.Level+1, Item.Item.LeftChild) ) {
                dprintf("Item %08x`%08x was duplicated!\n",
                    HIDWORD(Item.Item.LeftChild), LODWORD(Item.Item.LeftChild));
            }
        }

        if ( Item.Item.RightChild ) {
            if ( !AddTreeItem(Item.Level+1, Item.Item.RightChild) ) {
                dprintf("Item %08x`%08x was duplicated!\n",
                    HIDWORD(Item.Item.RightChild), LODWORD(Item.Item.RightChild));
            }
        }

        ++ItemCount;
        TraverseQueue.pop();
    }
}
