// GetFieldOffset is supported only in 64bit pointer mode.
#define KDEXT_64BIT

#include <windows.h>
#include <atlbase.h>
#include <dbgeng.h>
#include <wdbgexts.h>

#include <functional>
#include <memory>
#include <vector>

#include "common.h"
#include "page.h"

template <typename T, typename U>
T* at(void* base, U offset) {
  return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset);
}

int64_t extract(address_t n, int pos, int len) {
  return static_cast<int64_t>((n >> pos) & ((1ull << len) - 1));
}

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

  bool ReadPhysical(address_t addr, uint8_t* buffer, uint32_t size) {
    CComQIPtr<IDebugDataSpaces4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugDataSpaces4 failed\n");
      return false;
    }
    ULONG bytesRead = 0;
    HRESULT hr = fetcher->ReadPhysical(addr, buffer, size, &bytesRead);
    if (FAILED(hr)) {
      Log(L"IDebugDataSpaces4::ReadPhysical(%hs) failed\n",
          address_string(addr));
      return false;
    }
    return true;
  }

  template <typename T>
  bool ReadVirtual(address_t addr, T& outValue) {
    CComQIPtr<IDebugDataSpaces4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugDataSpaces4 failed\n");
      return false;
    }
    ULONG bytesRead = 0;
    HRESULT hr = fetcher->ReadVirtual(
        addr, &outValue, sizeof(T), &bytesRead);
    if (FAILED(hr)) {
      Log(L"IDebugDataSpaces4::ReadVirtual(%hs) failed\n",
          address_string(addr));
      return false;
    }
    return true;
  }

  bool ReadVirtual(address_t addr, uint8_t* buffer, uint32_t size) {
    CComQIPtr<IDebugDataSpaces4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugDataSpaces4 failed\n");
      return false;
    }
    ULONG bytesRead = 0;
    HRESULT hr = fetcher->ReadVirtual(addr, buffer, size, &bytesRead);
    if (FAILED(hr)) {
      Log(L"IDebugDataSpaces4::ReadVirtual(%hs) failed\n",
          address_string(addr));
      return false;
    }
    return true;
  }

  uint32_t GetTypeSize(LPCSTR type) {
    CComQIPtr<IDebugSymbols4> fetcher = client_;
    if (!fetcher) {
      Log(L"QI to IDebugSymbols4 failed\n");
      return 0;
    }
    HRESULT hr;
    ULONG64 base;
    ULONG typeId, typeSize;
    hr = fetcher->GetSymbolTypeId(type, &typeId, &base);
    if (FAILED(hr)) {
      Log(L"IDebugSymbols4::GetSymbolTypeId failed - %08lx\n", hr);
      return 0;
    }
    hr = fetcher->GetTypeSize(base, typeId, &typeSize);
    if (FAILED(hr)) {
      Log(L"IDebugSymbols4::GetTypeSize failed - %08lx\n", hr);
      return 0;
    }
    return typeSize;
  }
};

address_t GetPteBase(CommandRunner& runner) {
  address_t p, base;
  if (!runner.Evaluate("nt!MmPteBase", p)
      || !runner.ReadVirtual(p, base)) {
    Log(L"Failed to retrieve nt!MmPteBase\n");
    return 0;
  }
  return base;
}

class Paging;

enum class PagingMode {Invalid, None, B32, PAE, L4, L4PCID};
const char* PagingModeLabel(PagingMode mode) {
  static const char kLabels[][20] = {
      "",
      "None", "32-bit", "PAE",
      "4-level",
      "4-level (with PCID)",
      };
  return kLabels[static_cast<int>(mode)];
}

class TranslationResultBase {
 protected:
  PagingMode mode_;
  address_t dirBase_, virtAddr_;

  TranslationResultBase(PagingMode mode, address_t addr)
    : mode_(mode), virtAddr_(addr)
  {}

  template<typename T>
  static std::string GetCommonAttributesString(const T& entry) {
    std::string attrs;
    if (!entry.p) {
      attrs = " (inactive)";
    }
    else {
      if (entry.xd) attrs += " XD";
      attrs += (entry.rw ? " W" : " R");
      attrs += (entry.us ? " U" : " S");
      if (entry.pwt) attrs += " PWT";
      if (entry.pcd) attrs += " PCD";
      if (entry.a) attrs += " A";
    }
    return attrs;
  }

  std::vector<address_t> GetPageTableHierarchyForVirtual(
      CommandRunner& runner, address_t virt) const {
    std::vector<address_t> ret;
    address_t pteBase, addr;
    switch (mode_) {
      default: break;

      case PagingMode::L4:
        ret = std::vector<address_t>(5);
        pteBase = GetPteBase(runner);
        if (extract(pteBase, 0, 39) || extract(pteBase, 48, 16) != 0xffff) {
          runner.Printf("Invalid PTE Base - %s\n", address_string(pteBase));
          break;
        }

        addr = virt;
        for (int i = 0; i < 5; ++i) {
          // Based on the formula in kdexts!DbgGetPteAddress
          ret[i] = addr = pteBase + extract(addr, 12, 36) * 8;
        }
        break;

      case PagingMode::PAE:
        ret = std::vector<address_t>(4);
        pteBase = 0xc0000000;
        addr = virt;
        for (int i = 0; i < 4; ++i) {
          // Based on the formula in nt!MiGetPteAddress
          ret[i] = addr = pteBase + extract(addr, 12, 20) * 8;
        }
        break;
    }

    std::reverse(ret.begin(), ret.end());
    return ret;
  }

 public:
  virtual ~TranslationResultBase() = default;
  virtual void Print(CommandRunner& runner) const = 0;
};

class TranslationResult32Bit : public TranslationResultBase {
  PDEntry pde_;
  PTEntry pte_;

 public:
  TranslationResult32Bit(address_t base, address_t addr,
                         PDEntry pde, PTEntry pte)
    : TranslationResultBase(PagingMode::B32, addr), pde_(pde), pte_(pte)
  { dirBase_ = base; }

  TranslationResult32Bit(address_t base, address_t addr, PDEntry pde)
    : TranslationResultBase(PagingMode::B32, addr), pde_(pde), pte_{0}
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    runner.Printf("PagingMode: 32-bit\n"
                  "DirBase:    %s\n"
                  "PageDirTable @%-4d= %s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 22, 10)),
                    address_string(pde_.raw));
    if (!pde_.p) return;

    runner.Printf("PageTable    @%-4d= %s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 12, 10)),
                    address_string(pte_.raw));
  }
};

class TranslationResult32BitLarge : public TranslationResultBase {
  PDEntry4MB pde_;

 public:
  TranslationResult32BitLarge(address_t base, address_t addr,
                              PDEntry4MB pde)
    : TranslationResultBase(PagingMode::B32, addr), pde_(pde)
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    runner.Printf("PagingMode: 32-bit\n"
                  "DirBase:    %s\n"
                  "PageDirTable @%-4d= %s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 22, 10)),
                    address_string(pde_.raw));
  }
};

class TranslationResultPae : public TranslationResultBase {
  PDPTEntry pdpte_;
  PDEntry pde_;
  PTEntry pte_;

 public:
  TranslationResultPae(address_t base, address_t addr,
                       PDPTEntry pdpte, PDEntry pde, PTEntry pte)
    : TranslationResultBase(PagingMode::PAE, addr),
      pdpte_(pdpte), pde_(pde), pte_(pte)
  { dirBase_ = base; }

  TranslationResultPae(address_t base, address_t addr,
                       PDPTEntry pdpte, PDEntry pde)
    : TranslationResultBase(PagingMode::PAE, addr),
      pdpte_(pdpte), pde_(pde), pte_{0}
  { dirBase_ = base; }

  TranslationResultPae(address_t base, address_t addr,
                       PDPTEntry pdpte)
    : TranslationResultBase(PagingMode::PAE, addr),
      pdpte_(pdpte), pde_{0}, pte_{0}
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    auto virtPtes = GetPageTableHierarchyForVirtual(runner, virtAddr_);
    runner.Printf("PagingMode: PAE\n"
                  "DirBase:    %s\n"
                  "PageDirPointerTable @%-4d         = %s%s\n",
                  address_string(dirBase_),
                  static_cast<uint32_t>(extract(virtAddr_, 30, 2)),
                    address_string(pdpte_.raw),
                    GetCommonAttributesString(pdpte_).c_str());
    if (!pdpte_.p) return;

    runner.Printf("PageDirTable        @%-4d%s = %s%s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 21, 9)),
                    address_string(virtPtes[2]),
                    address_string(pde_.raw),
                    GetCommonAttributesString(pde_).c_str());
    if (!pde_.p) return;

    runner.Printf("PageTable           @%-4d%s = %s%s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 12, 9)),
                    address_string(virtPtes[3]),
                    address_string(pte_.raw),
                    GetCommonAttributesString(pte_).c_str());
  }
};

class TranslationResultPaeLarge : public TranslationResultBase {
  PDPTEntry pdpte_;
  PDEntry2MB pde_;

 public:
  TranslationResultPaeLarge(address_t base, address_t addr,
                            PDPTEntry pdpte, PDEntry2MB pde)
    : TranslationResultBase(PagingMode::PAE, addr),
      pdpte_(pdpte), pde_(pde)
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    auto virtPtes = GetPageTableHierarchyForVirtual(runner, virtAddr_);
    runner.Printf("PagingMode: PAE\n"
                  "DirBase:    %s\n"
                  "PageDirPointerTable @%-4d         = %s%s\n"
                  "PageDirTable        @%-4d%s = %s (2MB Page)%s\n"
                  "PageTable                %s   (2MB Page)\n",
                  address_string(dirBase_),
                  static_cast<uint32_t>(extract(virtAddr_, 30, 2)),
                    address_string(pdpte_.raw),
                    GetCommonAttributesString(pdpte_).c_str(),
                  static_cast<uint32_t>(extract(virtAddr_, 21, 9)),
                    address_string(virtPtes[2]),
                    address_string(pde_.raw),
                    GetCommonAttributesString(pde_).c_str(),
                  address_string(virtPtes[3]));
  }
};

class TranslationResultPml4 : public TranslationResultBase {
  PML4Entry pml4e_;
  PDPTEntry pdpte_;
  PDEntry pde_;
  PTEntry pte_;

 public:
  TranslationResultPml4(address_t base, address_t addr,
                        PML4Entry pml4e, PDPTEntry pdpte,
                        PDEntry pde, PTEntry pte)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_(pdpte), pde_(pde), pte_(pte)
  { dirBase_ = base; }

  TranslationResultPml4(address_t base, address_t addr,
                        PML4Entry pml4e, PDPTEntry pdpte, PDEntry pde)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_(pdpte), pde_(pde), pte_{0}
  { dirBase_ = base; }

  TranslationResultPml4(address_t base, address_t addr,
                        PML4Entry pml4e, PDPTEntry pdpte)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_(pdpte), pde_{0}, pte_{0}
  { dirBase_ = base; }

  TranslationResultPml4(address_t base, address_t addr,
                        PML4Entry pml4e)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_{0}, pde_{0}, pte_{0}
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    auto virtPtes = GetPageTableHierarchyForVirtual(runner, virtAddr_);
    runner.Printf("PagingMode: 4-level\n"
                  "DirBase:    %s %s\n"
                  "PML4 Table          @%-4d%s = %s%s\n",
                  address_string(dirBase_),
                    address_string(virtPtes[0]),
                  static_cast<uint32_t>(extract(virtAddr_, 39, 9)),
                    address_string(virtPtes[1]),
                    address_string(pml4e_.raw),
                    GetCommonAttributesString(pml4e_).c_str());
    if (!pml4e_.p) return;

    runner.Printf("PageDirPointerTable @%-4d%s = %s%s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 30, 9)),
                    address_string(virtPtes[2]),
                    address_string(pdpte_.raw),
                    GetCommonAttributesString(pdpte_).c_str());
    if (!pdpte_.p) return;

    runner.Printf("PageDirTable        @%-4d%s = %s%s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 21, 9)),
                    address_string(virtPtes[3]),
                    address_string(pde_.raw),
                    GetCommonAttributesString(pde_).c_str());
    if (!pde_.p) return;

    runner.Printf("PageTable           @%-4d%s = %s%s\n",
                  static_cast<uint32_t>(extract(virtAddr_, 12, 9)),
                    address_string(virtPtes[4]),
                    address_string(pte_.raw),
                    GetCommonAttributesString(pte_).c_str());
  }
};

class TranslationResultPml4Large2M : public TranslationResultBase {
  PML4Entry pml4e_;
  PDPTEntry pdpte_;
  PDEntry2MB pde_;

 public:
  TranslationResultPml4Large2M(address_t base, address_t addr,
                               PML4Entry pml4e, PDPTEntry pdpte, PDEntry2MB pde)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_(pdpte), pde_(pde)
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    auto virtPtes = GetPageTableHierarchyForVirtual(runner, virtAddr_);
    runner.Printf("PagingMode: 4-level\n"
                  "DirBase:    %s %s\n"
                  "PML4 Table          @%-4d%s = %s%s\n"
                  "PageDirPointerTable @%-4d%s = %s%s\n"
                  "PageDirTable        @%-4d%s = %s (2MB Page)%s\n"
                  "PageTable                %s   (2MB Page)\n",
                  address_string(dirBase_),
                    address_string(virtPtes[0]),
                  static_cast<uint32_t>(extract(virtAddr_, 39, 9)),
                    address_string(virtPtes[1]),
                    address_string(pml4e_.raw),
                    GetCommonAttributesString(pml4e_).c_str(),
                  static_cast<uint32_t>(extract(virtAddr_, 30, 9)),
                    address_string(virtPtes[2]),
                    address_string(pdpte_.raw),
                    GetCommonAttributesString(pdpte_).c_str(),
                  static_cast<uint32_t>(extract(virtAddr_, 21, 9)),
                    address_string(virtPtes[3]),
                    address_string(pde_.raw),
                    GetCommonAttributesString(pde_).c_str(),
                  address_string(virtPtes[4]));
  }
};

class TranslationResultPml4Large1G : public TranslationResultBase {
  PML4Entry pml4e_;
  PDPTEntry1GB pdpte_;

 public:
  TranslationResultPml4Large1G(address_t base, address_t addr,
                               PML4Entry pml4e, PDPTEntry1GB pdpte)
    : TranslationResultBase(PagingMode::L4, addr),
      pml4e_(pml4e), pdpte_(pdpte)
  { dirBase_ = base; }

  void Print(CommandRunner& runner) const override {
    auto virtPtes = GetPageTableHierarchyForVirtual(runner, virtAddr_);
    runner.Printf("PagingMode: 4-level\n"
                  "DirBase:    %s %s\n"
                  "PML4 Table          @%-4d%s = %s%s\n"
                  "PageDirPointerTable @%-4d%s = %s (1GB Page)%s\n"
                  "PageDirTable             %s   (1GB Page)\n"
                  "PageTable                %s   (1GB Page)\n",
                  address_string(dirBase_),
                    address_string(virtPtes[0]),
                  static_cast<uint32_t>(extract(virtAddr_, 39, 9)),
                    address_string(virtPtes[1]),
                    address_string(pml4e_.raw),
                    GetCommonAttributesString(pml4e_).c_str(),
                  static_cast<uint32_t>(extract(virtAddr_, 30, 9)),
                    address_string(virtPtes[2]),
                    address_string(pdpte_.raw),
                    GetCommonAttributesString(pdpte_).c_str(),
                  address_string(virtPtes[3]),
                  address_string(virtPtes[4]));
  }
};

class Paging {
  CommandRunner& runner_;
  PagingMode mode_;
  address_t base_;
  std::unique_ptr<TranslationResultBase> result_;

  bool Translate32Bit(address_t virt, address_t& phys) {
    PDEntry pde;
    if (!runner_.ReadPhysical(base_ + extract(virt, 22, 10) * 4, pde.raw))
      return false;
    if (!pde.p) {
      result_ = std::make_unique<TranslationResult32Bit>(base_, virt, pde);
      return false;
    }

    if (pde.ps) {
      PDEntry4MB pde_4mb;
      pde_4mb.raw = pde.raw;
      phys = extract(virt, 0, 22)
          | (pde_4mb.to_page << 22)
          | (pde_4mb.page_high << 32);
      result_ = std::make_unique<TranslationResult32BitLarge>(
          base_, virt, pde_4mb);
      return true;
    }

    PTEntry pte;
    if (!runner_.ReadPhysical(((pde.to_pt & 0xfffff) << 12)
                                  + extract(virt, 12, 10) * 4,
                              pte.raw))
      return false;

    result_ = std::make_unique<TranslationResult32Bit>(base_, virt, pde, pte);
    if (!pte.p) return false;

    phys = ((pte.to_page & 0xfffff) << 12) | extract(virt, 0, 12);
    return true;
  }

  bool TranslatePAE(address_t virt, address_t& phys) {
    PDPTEntry pdpte;
    if (!runner_.ReadPhysical(base_ + extract(virt, 30, 2) * 8, pdpte.raw))
      return false;
    if (!pdpte.p) {
      result_ = std::make_unique<TranslationResultPae>(base_, virt, pdpte);
      return false;
    }

    PDEntry pde;
    if (!runner_.ReadPhysical((pdpte.to_pd << 12) + extract(virt, 21, 9) * 8,
                              pde.raw))
      return false;
    if (!pde.p) {
      result_ = std::make_unique<TranslationResultPae>(base_, virt, pdpte, pde);
      return false;
    }

    if (pde.ps) {
      PDEntry2MB pde_2mb;
      pde_2mb.raw = pde.raw;
      phys = (pde_2mb.to_page << 21) | extract(virt, 0, 21);
      result_ = std::make_unique<TranslationResultPaeLarge>(
          base_, virt, pdpte, pde_2mb);
      return true;
    }

    PTEntry pte;
    if (!runner_.ReadPhysical((pde.to_pt << 12) + extract(virt, 12, 9) * 8,
                              pte.raw))
      return false;

    result_ = std::make_unique<TranslationResultPae>(
        base_, virt, pdpte, pde, pte);
    if (!pte.p) return false;

    phys = (pte.to_page << 12) | extract(virt, 0, 12);
    return true;
  }

  bool TranslatePML4(address_t virt, address_t& phys) {
    PML4Entry pml4e;
    if (!runner_.ReadPhysical(base_ + extract(virt, 39, 9) * 8, pml4e.raw))
      return false;
    if (!pml4e.p) {
      result_ = std::make_unique<TranslationResultPml4>(base_, virt, pml4e);
      return false;
    }

    PDPTEntry pdpte;
    if (!runner_.ReadPhysical((pml4e.to_pdpt << 12) + extract(virt, 30, 9) * 8,
                              pdpte.raw))
      return false;
    if (!pdpte.p) {
      result_ = std::make_unique<TranslationResultPml4>(
          base_, virt, pml4e, pdpte);
      return false;
    }

    if (pdpte.ps) {
      PDPTEntry1GB pdpte_1gb;
      pdpte_1gb.raw = pdpte.raw;
      phys = (pdpte_1gb.to_page << 30) | extract(virt, 0, 30);
      result_ = std::make_unique<TranslationResultPml4Large1G>(
          base_, virt, pml4e, pdpte_1gb);
      return true;
    }

    PDEntry pde;
    if (!runner_.ReadPhysical((pdpte.to_pd << 12) + extract(virt, 21, 9) * 8,
                              pde.raw))
      return false;
    if (!pde.p) {
      result_ = std::make_unique<TranslationResultPml4>(
          base_, virt, pml4e, pdpte, pde);
      return false;
    }

    if (pde.ps) {
      PDEntry2MB pde_2mb;
      pde_2mb.raw = pde.raw;
      phys = (pde_2mb.to_page << 21) | extract(virt, 0, 21);
      result_ = std::make_unique<TranslationResultPml4Large2M>(
          base_, virt, pml4e, pdpte, pde_2mb);
      return true;
    }

    PTEntry pte;
    if (!runner_.ReadPhysical((pde.to_pt << 12) + extract(virt, 12, 9) * 8,
                              pte.raw))
      return false;

    result_ = std::make_unique<TranslationResultPml4>(
        base_, virt, pml4e, pdpte, pde, pte);
    if (!pte.p) return false;

    phys = (pte.to_page << 12) | extract(virt, 0, 12);
    return true;
  }

 public:
  Paging(CommandRunner& runner, address_t base, bool maybe32bit)
      : runner_(runner), mode_(PagingMode::Invalid), base_(base) {
    HRESULT hr = runner_->IsPointer64Bit();
    switch (hr) {
      default: return;
      case S_OK:
        mode_ = PagingMode::L4;
        break;
      case S_FALSE:
        mode_ = maybe32bit ? PagingMode::B32 : PagingMode::PAE;
        break;
    }
  }

  Paging(CommandRunner& runner)
      : runner_(runner), mode_(PagingMode::Invalid), base_(0) {
    address_t cr0, cr3, cr4, ia32_efer;
    if (!runner_.Evaluate("@cr0", cr0)
        || !runner_.Evaluate("@cr3", cr3)
        || !runner_.Evaluate("@cr4", cr4)) {
      runner_.Printf("Failed to get control registers.\n");
      return;
    }

    if (!runner_.GetMsr(0xc0000080, ia32_efer)) {
      runner_.Printf("Failed to get msr[c0000080].  ");
      if (runner_->IsPointer64Bit() == S_OK) {
        runner_.Printf("Assuming LME is on.\n");
        ia32_efer = 1 << 8;
      }
      else {
        runner_.Printf("Assuming LME is off.\n");
        ia32_efer = 0;
      }
    }

    const bool pg = cr0 & (1 << 31),
               pae = cr4 & (1 << 5),
               lme = ia32_efer & (1 << 8),
               pcide = cr4 & (1 << 17);
    mode_ = pg
        ? (pae ? (lme ? (pcide ? PagingMode::L4PCID : PagingMode::L4)
                      : PagingMode::PAE)
               : PagingMode::B32)
        : PagingMode::None;

    switch (mode_) {
      case PagingMode::Invalid:
      case PagingMode::None:
        return;

      case PagingMode::B32:
        base_ = cr3 & 0xfffff000;
        break;
      case PagingMode::PAE:
        base_ = cr3 & 0xffffffe0;
        break;
      case PagingMode::L4:
      case PagingMode::L4PCID:
        base_ = cr3 & 0xffffffffff000;
        break;
    }
  }

  operator bool() const { return mode_ != PagingMode::Invalid; }
  void PrintResult() { if (result_) result_->Print(runner_); }

  bool Translate(address_t virt, address_t& phys) {
    switch (mode_) {
      case PagingMode::Invalid:
      default:
        return false;

      case PagingMode::None:
        phys = virt;
        return true;
      case PagingMode::B32:
        return Translate32Bit(virt, phys);
      case PagingMode::PAE:
        return TranslatePAE(virt, phys);
      case PagingMode::L4:
      case PagingMode::L4PCID:
        return TranslatePML4(virt, phys);
    }
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

    bool maybe32b = (vargs.size() >= 3) ? vargs[2] == "32" : false;
    paging = std::make_unique<Paging>(runner, dirbase, maybe32b);
  }

  address_t phys;
  bool translated = paging->Translate(virt, phys);
  paging->PrintResult();
  if (translated) {
    runner.Printf("Physical Address = %s\n", address_string(phys));
  }
}
