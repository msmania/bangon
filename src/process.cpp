// GetFieldOffset is supported only in 64bit pointer mode.
#define KDEXT_64BIT

#include <atlbase.h>
#include <dbgeng.h>
#include <memory>
#include <wdbgexts.h>
#include <windows.h>

#include "common.h"

std::wstring ReadUnicodeString(address_t addr, IDebugDataSpaces4 *reader) {
  static bool sSymbolLoad = false;
  static uint32_t sOffset_Length = 0;
  static uint32_t sOffset_Buffer = 0;
  if (!sSymbolLoad) {
    sOffset_Length = get_field_offset("ntdll!_UNICODE_STRING", "Length");
    sOffset_Buffer = get_field_offset("ntdll!_UNICODE_STRING", "Buffer");
    sSymbolLoad = true;
  }

  ULONG read;
  uint32_t len;
  if (FAILED(reader->ReadVirtual(addr + sOffset_Length, &len, sizeof(len),
                                 &read))) {
    return {};
  }

  address_t buf_addr;
  if (FAILED(reader->ReadVirtual(addr + sOffset_Buffer, &buf_addr,
                                 sizeof(buf_addr), &read))) {
    return {};
  }

  std::unique_ptr<uint8_t[]> buf(new uint8_t[len + sizeof(wchar_t)]);
  if (FAILED(reader->ReadVirtual(buf_addr, buf.get(), len, &read))) {
    return {};
  }

  return reinterpret_cast<const wchar_t *>(buf.get());
}

class LdrDataTableEntry {
  static bool sSymbolLoad;
  static uint32_t sOffset_InMemoryOrderLinks;
  static uint32_t sOffset_DllBase;
  static uint32_t sOffset_FullDllName;
  static uint32_t sOffset_ReferenceCount;
  static uint32_t sOffset_DdagNode;
  static uint32_t sOffset_Ddag_LoadCount;
  static uint32_t sOffset_ListFlink;

  static void StaticInit() {
    if (!sSymbolLoad) {
      sOffset_InMemoryOrderLinks =
          get_field_offset("ntdll!_LDR_DATA_TABLE_ENTRY", "InMemoryOrderLinks");
      sOffset_DllBase =
          get_field_offset("ntdll!_LDR_DATA_TABLE_ENTRY", "DllBase");
      sOffset_FullDllName =
          get_field_offset("ntdll!_LDR_DATA_TABLE_ENTRY", "FullDllName");
      sOffset_ReferenceCount =
          get_field_offset("ntdll!_LDR_DATA_TABLE_ENTRY", "ReferenceCount");
      sOffset_DdagNode =
          get_field_offset("ntdll!_LDR_DATA_TABLE_ENTRY", "DdagNode");
      sOffset_Ddag_LoadCount =
          get_field_offset("ntdll!_LDR_DDAG_NODE", "LoadCount");
      sOffset_ListFlink = get_field_offset("ntdll!_LIST_ENTRY", "Flink");
    }
  }

  address_t mAddr;

  LdrDataTableEntry(address_t addr) : mAddr(addr) { StaticInit(); }

public:
  static LdrDataTableEntry FromMemoryOrderModuleList(address_t listStart) {
    StaticInit();
    return listStart - sOffset_InMemoryOrderLinks;
  }

  LdrDataTableEntry() : LdrDataTableEntry(0) {}

  constexpr operator bool() const { return mAddr; }
  constexpr operator address_t() const { return mAddr; }

  bool operator==(const LdrDataTableEntry &other) const {
    return mAddr == other.mAddr;
  }
  bool operator!=(const LdrDataTableEntry &other) const {
    return mAddr != other.mAddr;
  }

  LdrDataTableEntry NextInMemoryOrder(IDebugDataSpaces4 *reader) const {
    ULONG read;
    address_t next;
    if (FAILED(reader->ReadVirtual(mAddr + sOffset_ListFlink, &next,
                                   sizeof(next), &read))) {
      return 0;
    }
    return next;
  }

  void Print(IDebugDataSpaces4 *reader) const {
    ULONG read;

    address_t base = 0;
    reader->ReadVirtual(mAddr + sOffset_DllBase, &base, sizeof(base), &read);
    if (!base) {
      return;
    }

    uint32_t refcount = 0;
    reader->ReadVirtual(mAddr + sOffset_ReferenceCount, &refcount,
                        sizeof(refcount), &read);

    auto name = ReadUnicodeString(mAddr + sOffset_FullDllName, reader);

    address_t ddag;
    reader->ReadVirtual(mAddr + sOffset_DdagNode, &ddag,
                        sizeof(ddag), &read);

    int32_t loadcount = 0;
    reader->ReadVirtual(ddag + sOffset_Ddag_LoadCount, &loadcount,
                        sizeof(loadcount), &read);

    dprintf("%p %p%3u%3d %S\n", reinterpret_cast<void *>(mAddr),
            reinterpret_cast<void *>(base), refcount, loadcount, name.c_str());
  }
};

bool LdrDataTableEntry::sSymbolLoad = false;
uint32_t LdrDataTableEntry::sOffset_InMemoryOrderLinks = 0;
uint32_t LdrDataTableEntry::sOffset_DllBase = 0;
uint32_t LdrDataTableEntry::sOffset_FullDllName = 0;
uint32_t LdrDataTableEntry::sOffset_ReferenceCount = 0;
uint32_t LdrDataTableEntry::sOffset_DdagNode = 0;
uint32_t LdrDataTableEntry::sOffset_Ddag_LoadCount = 0;
uint32_t LdrDataTableEntry::sOffset_ListFlink = 0;

DECLARE_API(proc) {
  CComPtr<IDebugClient7> client;
  if (FAILED(::DebugCreate(IID_PPV_ARGS(&client)))) {
    return;
  }

  CComQIPtr<IDebugSystemObjects4> obj = client;
  if (!obj) {
    return;
  }

  address_t peb;
  if (FAILED(obj->GetCurrentProcessPeb(&peb))) {
    return;
  }

  uint32_t offset_peb_ldr = get_field_offset("ntdll!_PEB", "Ldr");
  uint32_t offset_ldr_list =
      get_field_offset("ntdll!_PEB_LDR_DATA", "InMemoryOrderModuleList");

  CComQIPtr<IDebugDataSpaces4> reader = client;
  if (!reader) {
    return;
  }

  address_t ldr;
  ULONG read;
  if (FAILED(reader->ReadVirtual(peb + offset_peb_ldr, &ldr, sizeof(ldr),
                                 &read))) {
    return;
  }

  address_t list_start;
  if (FAILED(reader->ReadVirtual(ldr + offset_ldr_list, &list_start,
                                 sizeof(list_start), &read))) {
    return;
  }

  LdrDataTableEntry start =
      LdrDataTableEntry::FromMemoryOrderModuleList(list_start);
  for (LdrDataTableEntry entry = start;;) {
    entry.Print(reader);
    entry = entry.NextInMemoryOrder(reader);
    if (!entry || entry == start) {
      break;
    }
  }
}
