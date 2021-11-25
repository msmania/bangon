#include <windows.h>
#include <strsafe.h>
#include <atlbase.h>
#include <dbgeng.h>
#include <wdbgexts.h>

#include "common.h"

struct Symbol {
  const char* label_;
  uint64_t value_;
};

Symbol kProtections[] = {
  {"PAGE_NOACCESS", PAGE_NOACCESS},
  {"PAGE_READONLY", PAGE_READONLY},
  {"PAGE_READWRITE", PAGE_READWRITE},
  {"PAGE_WRITECOPY", PAGE_WRITECOPY},
  {"PAGE_EXECUTE", PAGE_EXECUTE},
  {"PAGE_EXECUTE_READ", PAGE_EXECUTE_READ},
  {"PAGE_EXECUTE_READWRITE", PAGE_EXECUTE_READWRITE},
  {"PAGE_EXECUTE_WRITECOPY", PAGE_EXECUTE_WRITECOPY},
  {"PAGE_GUARD", PAGE_GUARD},
  {"PAGE_NOCACHE", PAGE_NOCACHE},
  {"PAGE_WRITECOMBINE", PAGE_WRITECOMBINE},
  {nullptr, 0},
};

Symbol kStates[] = {
  {"MEM_COMMIT", MEM_COMMIT},
  {"MEM_RESERVE", MEM_RESERVE},
  {"MEM_FREE", MEM_FREE},
  {nullptr, 0},
};

Symbol kTypes[] = {
  {"MEM_IMAGE", MEM_IMAGE},
  {"MEM_MAPPED", MEM_MAPPED},
  {"MEM_PRIVATE", MEM_PRIVATE},
  {nullptr, 0},
};

std::string Symbolize(const Symbol symbols[], uint64_t flags) {
  std::string s;
  for (const Symbol* p = symbols; p->label_; ++p) {
    if (flags & p->value_) {
      if (!s.empty()) {
        s += '|';
      }
      s += p->label_;
      flags &= ~p->value_;
    }
  }

  if (flags) {
    if (!s.empty()) {
      s += '|';
    }
    s += std::to_string(flags);
  }

  return s;
}

std::string AddrToStr(ULONGLONG addr) {
  char buf[8 + 1 + 8 + 1];
  ::StringCbPrintfA(
      buf,
      sizeof(buf),
      "%08x`%08x",
      addr >> 32,
      addr & 0xffffffff);
  return buf;
}

DECLARE_API(mem) {
  CComPtr<IDebugClient7> client;

  HRESULT hr = ::DebugCreate(IID_PPV_ARGS(&client));
  if (FAILED(hr)) {
    Log(L"DebugCreate failed - %08lx\n", hr);
    return;
  }

  CComQIPtr<IDebugDataSpaces4> dataspaces = client;
  if (!dataspaces) {
    Log(L"QI to IDebugDataSpaces4 failed\n");
    return;
  }

  for (ULONGLONG addr = 0; ;) {
    MEMORY_BASIC_INFORMATION64 meminfo = {};
    hr = dataspaces->QueryVirtual(addr, &meminfo);
    if (hr == E_NOINTERFACE) {
      break;
    }
    if (FAILED(hr)) {
      Log(L"IDebugDataSpaces4::QueryVirtual failed - %08lx\n", hr);
      return;
    }

    std::string prot = Symbolize(kProtections, meminfo.Protect);
    std::string state = Symbolize(kStates, meminfo.State);
    std::string type = Symbolize(kTypes, meminfo.Type);

    std::string base = AddrToStr(meminfo.BaseAddress);
    std::string size = AddrToStr(meminfo.RegionSize);

    if (meminfo.AllocationProtect == meminfo.Protect
        || meminfo.AllocationProtect == 0) {
      dprintf("%s %s %s %s\n",
              base.c_str(),
              size.c_str(),
              state.c_str(),
              prot.c_str());
    }
    else {
      std::string prot0 = Symbolize(kProtections, meminfo.AllocationProtect);
      dprintf("%s %s %s %s (was %s)\n",
              base.c_str(),
              size.c_str(),
              state.c_str(),
              prot.c_str(),
              prot0.c_str());
    }

    addr = meminfo.BaseAddress + meminfo.RegionSize;
  }
}
