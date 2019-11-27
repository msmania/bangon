#include <vector>
#include <iostream>
#include <string>
#include <stdio.h>
#include <windows.h>
#define KDEXT_64BIT
#include <wdbgexts.h>
#include "common.h"

#define LODWORD(ll) ((uint32_t)((ll)&0xffffffff))
#define HIDWORD(ll) ((uint32_t)(((ll)>>32)&0xffffffff))

address_t load_pointer(address_t addr) {
  address_t loaded{};
  if (!ReadPointer(addr, &loaded)) {
    address_string s(addr);
    Log(L"Failed to load a pointer from %hs\n", s);
  }
  return loaded;
}

const char *ptos(uint64_t p, char *s, uint32_t len) {
  if (HIDWORD(p) == 0 && len >= 9)
    sprintf(s, "%08x", LODWORD(p));
  else if (HIDWORD(p) != 0 && len >= 18)
    sprintf(s, "%08x`%08x", HIDWORD(p), LODWORD(p));
  else if (len > 0)
    s[0] = 0;
  else
    s = nullptr;
  return s;
}

address_string::address_string(address_t addr) {
  ptos(addr, buffer_, sizeof(buffer_));
}

address_string::operator const char *() const {
  return buffer_;
}

void DumpAddressAndSymbol(std::ostream &s, address_t addr) {
  char symbol[1024];
  ULONG64 displacement;
  GetSymbol(addr, symbol, &displacement);
  s << address_string(addr)
    << ' ' << symbol;
  if (displacement) s << "+0x" << std::hex << displacement;
}

void Log(const wchar_t* format, ...) {
  wchar_t linebuf[1024];
  va_list v;
  va_start(v, format);
  wvsprintf(linebuf, format, v);
  va_end(v);
  OutputDebugString(linebuf);
}

std::vector<std::string> get_args(const char *args) {
  std::vector<std::string> string_array;
  const char *prev, *p;
  prev = p = args;
  while (*p) {
    if (*p == ' ') {
      if (p > prev)
        string_array.emplace_back(args, prev - args, p - prev);
      prev = p + 1;
    }
    ++p;
  }
  if (p > prev)
    string_array.emplace_back(args, prev - args, p - prev);
  return string_array;
}

#ifdef TEST
TEST(T, get_args) {
  EXPECT_THAT(get_args("1 2 3"), ::testing::ElementsAre("1", "2", "3"));
  EXPECT_THAT(get_args("  1   2 "), ::testing::ElementsAre("1", "2"));
  EXPECT_THAT(get_args("1"), ::testing::ElementsAre("1"));
  EXPECT_THAT(get_args(""), ::testing::ElementsAre());
  EXPECT_THAT(get_args("   "), ::testing::ElementsAre());
}
#endif

#if 0
void forEachThread(std::function<void(ULONG, ULONG)> callback) {
  CComPtr<IDebugClient7> client;
  if (SUCCEEDED(DebugCreate(IID_PPV_ARGS(&client)))) {
    if (CComQIPtr<IDebugSystemObjects4> system = client) {
      ULONG original_index, n;
      system->GetCurrentThreadId(&original_index);
      system->GetNumberThreads(&n);
      std::vector<ULONG> indexes(n), ids(n);
      system->GetThreadIdsByIndex(0, n, indexes.data(), ids.data());
      for (ULONG i = 0; i < n; ++i) {
        system->SetCurrentThreadId(indexes[i]);
        callback(indexes[i], ids[i]);
      }
      system->SetCurrentThreadId(original_index);
    }
  }
}
#endif

void dump_symbol_manager();
void dump_vtable_manager();

DECLARE_API(runtests) {
  dump_symbol_manager();
  dump_vtable_manager();
}
