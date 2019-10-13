// GetFieldOffset is supported only in 64bit pointer mode.
#define KDEXT_64BIT

#include <windows.h>
#include <atlbase.h>
#include <dbgeng.h>
#include <wdbgexts.h>

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <iostream>

#include "common.h"

void debug_object::dump(std::ostream &s) const {
  address_string s1(base_);
  s << s1 << std::endl;
}

ULONG GetFieldOffsetEx(LPCSTR Type, LPCSTR Field, PFIELD_INFO Info) {
    FIELD_INFO flds = {
        (PUCHAR)Field,
        (PUCHAR)"",
        0,
        DBG_DUMP_FIELD_FULL_NAME | DBG_DUMP_FIELD_RETURN_ADDRESS,
        0,
        NULL
    };
    SYM_DUMP_PARAM Sym = {
        sizeof (SYM_DUMP_PARAM),
        (PUCHAR)Type,
        DBG_DUMP_NO_PRINT,
        0,
        NULL,
        NULL,
        NULL,
        1,
        &flds
    };
    Sym.nFields = 1;
    ULONG Err = Ioctl(IG_DUMP_SYMBOL_INFO, &Sym, Sym.size);
    *Info = flds;
    return Err;
}

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

namespace {
  target_info target_info_instance;
}

void target_info::init() {
  CComPtr<IDebugClient7> client;
  if (SUCCEEDED(DebugCreate(IID_PPV_ARGS(&client)))) {
    if (CComQIPtr<IDebugControl3> control = client) {
      ULONG type;
      if (SUCCEEDED(control->GetActualProcessorType(&type))) {
        actualProcessorType = type;
      }
      if (SUCCEEDED(control->GetEffectiveProcessorType(&type))) {
        effectiveProcessorType = type;
      }
    }
  }
}

const target_info &target_info::get() {
  return target_info_instance;
}

void init_target_info() {
  target_info_instance.init();
}
