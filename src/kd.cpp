// GetFieldOffset is supported only in 64bit pointer mode.
#define KDEXT_64BIT

#include <windows.h>
#include <atlbase.h>
#include <dbgeng.h>
#include <wdbgexts.h>

#include <functional>
#include <memory>

#include "common.h"
#include "page.h"

class CommandRunner {
  class OutputCallback : public IDebugOutputCallbacks {
    ULONG ref_;
    std::function<void(ULONG, PCSTR)> callback_;

   public:
    OutputCallback() : ref_(1) {}

    operator bool() const { return !!callback_; }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) {
      const QITAB QITable[] = {
        QITABENT(OutputCallback, IDebugOutputCallbacks),
        { 0 },
      };
      return QISearch(this, QITable, riid, ppvObject);
    }
    STDMETHOD_(ULONG, AddRef)() {
      return InterlockedIncrement(&ref_);
    }
    STDMETHOD_(ULONG, Release)() {
      ULONG newRef = InterlockedDecrement(&ref_);
      if (newRef == 0) delete this;
      return newRef;
    }

    // IDebugOutputCallbacks
    STDMETHOD(Output)(ULONG Mask, PCSTR Text) {
      if (callback_) callback_(Mask, Text);
      return S_OK;
    }

    void SetCallback(std::function<void(ULONG, PCSTR)> callback) {
      callback_ = callback;
    }
  };

  CComPtr<IDebugClient7> client_;
  CComQIPtr<IDebugControl7> control_;
  OutputCallback outputCB_;

 public:
  CommandRunner() {
    HRESULT hr = DebugCreate(IID_PPV_ARGS(&client_));
    if (FAILED(hr)) {
      Log(L"DebugCreate failed - %08lx\n", hr);
      return;
    }

    hr = client_->SetOutputCallbacks(&outputCB_);
    if (FAILED(hr)) {
      Log(L"IDebugClient5::SetOutputCallbacks failed - %08lx\n", hr);
      return;
    }

    control_ = client_;
    if (!control_) {
      Log(L"QI to IDebugControl7 failed\n");
      return;
    }
  }

  operator bool() const { return !!client_ && !!control_; }
  IDebugControl7* operator->() { return control_; }

  void Printf(PCSTR format, ...) {
    if (!control_) return;
    if (outputCB_) return; // Prevent re-entrancy

    va_list v;
    va_start(v, format);
    control_->OutputVaList(DEBUG_OUTPUT_NORMAL, format, v);
    va_end(v);
  }

  bool Run(LPCSTR command, const std::function<void(ULONG, PCSTR)>& callback) {
    if (!control_) return false;

    outputCB_.SetCallback(callback);
    HRESULT hr = control_->Execute(DEBUG_OUTCTL_THIS_CLIENT,
                                  command,
                                  DEBUG_EXECUTE_NOT_LOGGED);
    outputCB_.SetCallback(nullptr);

    if (FAILED(hr)) {
      Log(L"IDebugControl::Execute failed - %08lx\n", hr);
      return false;
    }

    return true;
  }

  bool Evaluate(LPCSTR expression, address_t& outValue) {
    if (!control_) return false;

    DEBUG_VALUE val;
    HRESULT hr = control_->Evaluate(expression,
                                    DEBUG_VALUE_INT64,
                                    &val,
                                    nullptr);
    if (FAILED(hr)) {
      Log(L"IDebugControl::Evaluate failed - %08lx\n", hr);
      return false;
    }

    outValue = val.I64;
    return true;
  }

  bool GetMsr(ULONG msr, address_t& outValue) {
    CComQIPtr<IDebugDataSpaces4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugDataSpaces4 failed\n");
      return false;
    }
    return SUCCEEDED(fetcher->ReadMsr(msr, &outValue));
  }

  template <typename T>
  bool ReadPhysical(address_t addr, T& outValue) {
    CComQIPtr<IDebugDataSpaces4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugDataSpaces4 failed\n");
      return false;
    }
    ULONG bytesRead = 0;
    HRESULT hr = fetcher->ReadPhysical(
        addr, &outValue, sizeof(T), &bytesRead);
    if (FAILED(hr)) {
      Log(L"IDebugDataSpaces4::ReadPhysical(%hs) failed\n",
          address_string(addr));
      return false;
    }
    return true;
  }
};

class Paging {
  enum class Mode {Invalid, None, B32, PAE, L4, L4PCID};

  static const char* Label(Mode mode) {
    static const char kLabels[][20] = {
        "",
        "None", "32-bit", "PAE",
        "4-level",
        "4-level (with PCID)",
        };
    return kLabels[static_cast<int>(mode)];
  }

  CommandRunner& runner_;
  Mode mode_;
  address_t base_;

  template<typename T, bool RWUSA = true>
  bool Lookup(address_t table, uint32_t index, uint32_t width, T& out) {
    if (!runner_.ReadPhysical(table + index * width, out.raw))
      return false;

    std::string attrs;
    if (!out.p)
      attrs = " (inactive)";
    else {
      if (RWUSA) {
        attrs += (out.rw ? " W" : " R");
        attrs += (out.us ? " S" : " U");
      }
      if (out.pwt) attrs += " PWT";
      if (out.pcd) attrs += " PCD";
      if (RWUSA && out.a) attrs += " A";
    }

    runner_.Printf("%s @%-5d-> %s%s\n",
                   address_string(table), index,
                   address_string(out.raw),
                   attrs.c_str());
    return out.p;
  }

  bool Translate32Bit(address_t virt, address_t& phys) {
    PDEntry pde;
    if (!Lookup(base_, (virt >> 22) & 0x3ff, 4, pde))
      return false;

    if (pde.ps) {
      runner_.Printf("4MB page is enabled.\n");
      PDEntry4MB pde_4mb;
      pde_4mb.raw = pde.raw;
      phys = (virt & ((1 << 22) - 1))
          | (pde_4mb.to_page << 22)
          | (pde_4mb.page_high << 32);
      return true;
    }

    const address_t pt = (pde.to_pt & 0xfffff) << 12;

    PTEntry pte;
    if (!Lookup(pt, (virt >> 12) & 0x3ff, 4, pte))
      return false;

    phys = ((pte.to_page & 0xfffff) << 12) | (virt & 0xfff);
    return true;
  }

  bool TranslatePAE(address_t virt, address_t& phys) {
    PDPTEntry pdpte;
    if (!Lookup<PDPTEntry, false>(base_, (virt >> 30) & 0x3, 8, pdpte))
      return false;

    const address_t pd = pdpte.to_pd << 12;

    PDEntry pde;
    if (!Lookup(pd, (virt >> 21) & 0x1ff, 8, pde))
      return false;

    if (pde.ps) {
      runner_.Printf("2MB page is enabled.\n");
      PDEntry2MB pde_2mb;
      pde_2mb.raw = pde.raw;
      phys = (pde_2mb.to_page << 21) | (virt & ((1 << 21) - 1));
      return true;
    }

    const address_t pt = pde.to_pt << 12;

    PTEntry pte;
    if (!Lookup(pt, (virt >> 12) & 0x1ff, 8, pte))
      return false;

    phys = (pte.to_page << 12) | (virt & 0xfff);
    return true;
  }

  bool TranslatePML4(address_t virt, address_t& phys) {
    union VirtualAddress64 {
      address_t raw;
      struct {
        address_t offset : 12;
        address_t table : 9;
        address_t dir : 9;
        address_t dirp : 9;
        address_t pml4 : 9;
      };
    } va;
    va.raw = virt;

    PML4Entry pml4e;
    if (!Lookup(base_, va.pml4, 8, pml4e))
      return false;

    const address_t pdpt = pml4e.to_pdpt << 12;

    PDPTEntry pdpte;
    if (!Lookup(pdpt, va.dirp, 8, pdpte))
      return false;

    if (pdpte.ps) {
      runner_.Printf("1GB page is enabled.\n");
      PDPTEntry1GB pdpte_1gb;
      pdpte_1gb.raw = pdpte.raw;
      phys = (pdpte_1gb.to_page << 30) | (va.raw & ((1 << 30) - 1));
      return true;
    }

    const address_t pd = pdpte.to_pd << 12;

    PDEntry pde;
    if (!Lookup(pd, va.dir, 8, pde))
      return false;

    if (pde.ps) {
      runner_.Printf("2MB page is enabled.\n");
      PDEntry2MB pde_2mb;
      pde_2mb.raw = pde.raw;
      phys = (pde_2mb.to_page << 21) | (va.raw & ((1 << 21) - 1));
      return true;
    }

    const address_t pt = pde.to_pt << 12;

    PTEntry pte;
    if (!Lookup(pt, va.table, 8, pte))
      return false;

    phys = (pte.to_page << 12) | va.offset;
    return true;
  }

 public:
  Paging(CommandRunner& runner, address_t base, bool maybePAE)
      : runner_(runner), mode_(Mode::Invalid), base_(base) {
    HRESULT hr = runner_->IsPointer64Bit();
    switch (hr) {
      default: return;
      case S_OK:
        mode_ = Mode::L4;
        break;
      case S_FALSE:
        mode_ = maybePAE ? Mode::PAE : Mode::B32;
        break;
    }

    runner_.Printf("Assuming PagingMode is %s\n", Label(mode_));
    runner_.Printf("DirBase = %s\n", address_string(base_));
  }

  Paging(CommandRunner& runner)
      : runner_(runner), mode_(Mode::Invalid), base_(0) {
    address_t cr0, cr3, cr4, ia32_efer;
    if (!runner_.Evaluate("@cr0", cr0)
        || !runner_.Evaluate("@cr3", cr3)
        || !runner_.Evaluate("@cr4", cr4)
        || !runner_.GetMsr(0xc0000080, ia32_efer)
        ) {
      runner_.Printf("Failed to get control registers.\n");
      return;
    }

    const bool pg = cr0 & (1 << 31),
               pae = cr4 & (1 << 5),
               lme = ia32_efer & (1 << 8),
               pcide = cr4 & (1 << 17);
    mode_ = pg
        ? (pae ? (lme ? (pcide ? Mode::L4PCID : Mode::L4)
                      : Mode::PAE)
               : Mode::B32)
        : Mode::None;
    runner_.Printf("PagingMode: %s\n", Label(mode_));

    switch (mode_) {
      case Mode::Invalid:
      case Mode::None:
        return;

      case Mode::B32:
        base_ = cr3 & 0xfffff000;
        break;
      case Mode::PAE:
        base_ = cr3 & 0xffffffe0;
        break;
      case Mode::L4:
      case Mode::L4PCID:
        base_ = cr3 & 0xfffffffffffff000;
        break;
    }

    runner_.Printf("DirBase = %s\n", address_string(base_));
  }

  operator bool() const { return mode_ != Mode::Invalid; }

  bool Translate(address_t virt, address_t& phys) {
    switch (mode_) {
      case Mode::Invalid: return false;

      case Mode::None:
        phys = virt;
        return true;
      case Mode::B32:
        return Translate32Bit(virt, phys);
      case Mode::PAE:
        return TranslatePAE(virt, phys);
      case Mode::L4:
      case Mode::L4PCID:
        return TranslatePML4(virt, phys);
    }

    return false;
  }
};

DECLARE_API(v2p) {
  const auto vargs = get_args(args);
  if (vargs.size() == 0) return;

  CommandRunner runner;
  if (!runner) return;

  address_t virt;
  if (!runner.Evaluate(vargs[0].c_str(), virt)) return;
  if (runner->IsPointer64Bit() != S_OK) virt &= 0xffffffff;
  runner.Printf("Virtual address = %s\n", address_string(virt));

  std::unique_ptr<Paging> paging;
  if (vargs.size() == 1) {
    paging = std::make_unique<Paging>(runner);
  }
  else {
    address_t dirbase;
    if (!runner.Evaluate(vargs[1].c_str(), dirbase)) return;

    bool maybePAE = (vargs.size() >= 3) ? vargs[2] == "PAE" : false;
    paging = std::make_unique<Paging>(runner, dirbase, maybePAE);
  }

  address_t phys;
  if (!paging->Translate(virt, phys)) return;
  runner.Printf("Physical Address = %s\n", address_string(phys));
}
