#include <vector>
#include <functional>
#include <sstream>
#include <windows.h>
#define KDEXT_64BIT
#include <wdbgexts.h>
#include "common.h"

class Thread : public debug_object {
  address_t tlshead_{},
            threadstate_{};
  bool render_thread_;

  address_t GetTeb() {
    address_t teb;
    GetTebAddress(&teb);
    const auto &target = target_info::get();
    if (target.actualProcessorType == IMAGE_FILE_MACHINE_AMD64
        && target.effectiveProcessorType == IMAGE_FILE_MACHINE_I386) {
      teb = load_pointer(teb);
    }
    return teb;
  }

public:
  virtual void load(address_t addr) {
    if (addr == 0) addr = GetTeb();
    base_ = addr;
    const auto &target = target_info::get();
    if (target.effectiveProcessorType == IMAGE_FILE_MACHINE_AMD64) {
      tlshead_ = load_pointer(
        addr + get_field_offset("ntdll!_TEB", "ThreadLocalStoragePointer"));
    }
    else {
      tlshead_ = load_pointer(
        addr + get_field_offset("ntdll!_TEB32", "ThreadLocalStoragePointer"));
    }
  }

  virtual void dump(std::ostream &s) const {
    address_string s1(base_),
                   s2(tlshead_);
    s << "TEB " << s1
      << " TLSHEAD " << s2;
  }
};

void forEachThread(std::function<void(ULONG, ULONG)> callback);

DECLARE_API(ts) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0 && vargs[0] == "-all") {
    forEachThread([](ULONG idx, ULONG tid) {
      Thread t;
      t.load(0);
      std::stringstream ss;
      t.dump(ss);
      dprintf("%2d:%04x %s\n", idx, tid, ss.str().c_str());
    });
  }
  else {
    Thread t;
    t.load(0);
    std::stringstream ss;
    t.dump(ss);
    dprintf("%s\n", ss.str().c_str());
  }
}
