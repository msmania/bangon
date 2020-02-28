#include <functional>
#include <iomanip>
#include <sstream>
#include <vector>
#define KDEXT_64BIT
#include <windows.h>
#include <wdbgexts.h>

#include "common.h"
#include "peimage.h"

// https://docs.microsoft.com/en-us/windows/win32/debug/pe-format
// https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_data_directory
enum ImageDataDirectoryName {
  ExportTable,
  ImportTable,
  ResourceTable,
  ExceptionTable,
  CertificateTable,
  BaseRelocationTable,
  DebuggingInformation,
  ArchitectureSpecificData,
  GlobalPointerRegister,
  ThreadLocalStorageTable,
  LoadConfiguration,
  BoundImportTable,
  ImportAddressTable,
  DelayImportDescriptor,
  CLRHeader,
  Reserved,
};

PEImage::PEImage(address_t base) {
  Load(base);
}

PEImage::operator bool() const {
  return !!base_;
}

bool PEImage::IsInitialized() const {
  return !!base_;
}

bool PEImage::Is64bit() const {
  return is64bit_;
}

bool PEImage::Load(address_t base) {
  constexpr uint16_t MZ = 0x5a4d;
  constexpr uint32_t PE = 0x4550;
  constexpr uint16_t PE32 = 0x10b;
  constexpr uint16_t PE32PLUS = 0x20b;

  const auto dos = load_data<IMAGE_DOS_HEADER>(base);
  if (dos.e_magic != MZ) {
    dprintf("Invalid DOS header\n");
    return false;
  }

  if (load_data<uint32_t>(base + dos.e_lfanew) != PE) {
    dprintf("Invalid PE header\n");
    return false;
  }

  const auto fileHeader = load_data<IMAGE_FILE_HEADER>(
      base + dos.e_lfanew + sizeof(PE));
  const auto rvaOptHeader = base
    + dos.e_lfanew
    + sizeof(PE)
    + sizeof(IMAGE_FILE_HEADER);
  address_t sectionHeader = rvaOptHeader;
  switch (fileHeader.Machine) {
    default:
      dprintf("Unsupported platform - %04x.\n", fileHeader.Machine);
      return false;
    case IMAGE_FILE_MACHINE_AMD64: {
      const auto optHeader = load_data<IMAGE_OPTIONAL_HEADER64>(rvaOptHeader);
      sectionHeader += sizeof(IMAGE_OPTIONAL_HEADER64);
      if (optHeader.Magic != PE32PLUS) {
        dprintf("Invalid optional header\n");
        return false;
      }
      is64bit_ = true;
      for (int i = 0; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; ++i) {
        directories_[i] = optHeader.DataDirectory[i];
      }
      break;
    }
    case IMAGE_FILE_MACHINE_I386: {
      const auto optHeader = load_data<IMAGE_OPTIONAL_HEADER32>(rvaOptHeader);
      sectionHeader += sizeof(IMAGE_OPTIONAL_HEADER32);
      if (optHeader.Magic != PE32) {
        dprintf("Invalid optional header\n");
        return false;
      }
      is64bit_ = false;
      for (int i = 0; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; ++i) {
        directories_[i] = optHeader.DataDirectory[i];
      }
      break;
    }
  }

  for (int i = 0; i < fileHeader.NumberOfSections; ++i) {
    const auto sig = load_data<uint64_t>(sectionHeader);
    if (!sig) break;
    const auto section = load_data<IMAGE_SECTION_HEADER>(sectionHeader);
    sections_.push_back(section);
    sectionHeader += sizeof(IMAGE_SECTION_HEADER);
  }

  base_ = base;
  return true;
}

std::string PEImage::RvaString(uint32_t offset) const {
  struct CHAR100 {
    char p[100];
  } buf = load_data<CHAR100>(base_ + offset);
  return buf.p;
}

void PEImage::DumpIATEntries(int index,
                             std::ostream &s,
                             const IMAGE_IMPORT_DESCRIPTOR &desc) const {
  if (!IsInitialized()) return;

  const uint32_t address_size = Is64bit() ? 8 : 4;
  const address_t
    start_name = base_ + desc.OriginalFirstThunk,
    start_func = base_ + desc.FirstThunk;

  for (int index_entry = 0; ; ++index_entry) {
    const address_t
      rva_name = load_pointer(start_name + index_entry * address_size),
      func_entry = start_func + index_entry * address_size,
      rva_func = load_pointer(func_entry);
    if (!rva_name) break;

    if (rva_name & (Is64bit() ? IMAGE_ORDINAL_FLAG64 : IMAGE_ORDINAL_FLAG32)) {
      const uint16_t ordinal = rva_name & 0xffff;
      s << index << '.' << index_entry
        << " Ordinal#"  << std::dec << ordinal;
    }
    else {
      const auto hint = load_data<uint16_t>(base_ + rva_name);
      const auto name = RvaString(static_cast<uint32_t>(rva_name) + 2);
      s << index << '.' << index_entry
        << ' ' << name << '@' << hint;
    }

    s << ' ' << address_string(func_entry) << ' ';

    DumpAddressAndSymbol(s, rva_func);
    s << std::endl;
  }
}

void PEImage::DumpIAT(const std::string &target) const {
  if (!IsInitialized()) return;

  const address_t
    dir_start = base_ + directories_[ImportTable].VirtualAddress,
    dir_end = dir_start + directories_[ImportTable].Size;

  int index_desc = 0;
  for (address_t desc_raw = dir_start;
       desc_raw < dir_end;
       desc_raw += sizeof(IMAGE_IMPORT_DESCRIPTOR), ++index_desc) {
    const auto desc = load_data<IMAGE_IMPORT_DESCRIPTOR>(desc_raw);
    if (!desc.Characteristics) break;

    const auto thunk_name = RvaString(desc.Name);

    if (target.size() == 0 || target == "*") {
      std::stringstream s;
      s << std::dec << index_desc
        << ' ' << address_string(desc_raw)
        << ' ' << thunk_name
        << std::endl;
      dprintf("%s", s.str().c_str());
    }

    if (target == "*" || target == thunk_name) {
      std::stringstream s;
      DumpIATEntries(index_desc, s, desc);
      dprintf("%s\n", s.str().c_str());
    }
  }
}

template<typename T>
static void DumpLoadConfigInternal(address_t base, address_t dir_start) {
  const auto directory = load_data<T>(dir_start);

  {
    address_t addr;
    std::stringstream s;

    addr = directory.GuardCFCheckFunctionPointer;
    s << "GuardCFCheckFunctionPointer    "
      << address_string(addr);
    if (addr) {
      s << ' ';
      DumpAddressAndSymbol(s, load_pointer(addr));
    }
    s << std::endl;

    addr = directory.GuardCFDispatchFunctionPointer;
    s << "GuardCFDispatchFunctionPointer "
      << address_string(addr);
    if (addr) {
      s << ' ';
      DumpAddressAndSymbol(s, load_pointer(addr));
    }
    s << std::endl;

    s << "GuardFlags                     "
      << std::hex << std::setw(8) << directory.GuardFlags << std::endl
      << "GuardCFFunctionTable           "
      << address_string(directory.GuardCFFunctionTable) << std::endl;

    dprintf("%s", s.str().c_str());
  }

  // https://docs.microsoft.com/en-us/windows/win32/secbp/pe-metadata
  uint32_t item_size =
    directory.GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_MASK;
  item_size >>= IMAGE_GUARD_CF_FUNCTION_TABLE_SIZE_SHIFT;
  item_size += sizeof(DWORD);

  const auto table_size =
    static_cast<uint32_t>(item_size * directory.GuardCFFunctionCount);
  std::vector<uint8_t> buf(table_size);

  ULONG cb = 0;
  if (!ReadMemory(directory.GuardCFFunctionTable,
                  buf.data(), table_size, &cb)) {
    Log(L"Failed to load GuardCFFunctionTable\n");
    return;
  }

  uint8_t *p = buf.data();
  for (DWORD i = 0; i < directory.GuardCFFunctionCount; ++i) {
    uint32_t entry[2] = {};
    memcpy(entry, p, item_size);

    std::stringstream s;
    s << std::dec << std::setw(4) << i
      << std::setw(2) << entry[1] << ' ';
    DumpAddressAndSymbol(s, base + entry[0]);
    dprintf("%s\n", s.str().c_str());

    p += item_size;
  }
}

void PEImage::DumpLoadConfig() const {
  if (!IsInitialized()) return;

  if (!directories_[LoadConfiguration].VirtualAddress) return;

  const address_t
    dir_start = base_ + directories_[LoadConfiguration].VirtualAddress,
    dir_end = dir_start + directories_[LoadConfiguration].Size;

  if (Is64bit()) {
    DumpLoadConfigInternal<IMAGE_LOAD_CONFIG_DIRECTORY64>(base_, dir_start);
  }
  else {
    DumpLoadConfigInternal<IMAGE_LOAD_CONFIG_DIRECTORY32>(base_, dir_start);
  }
}

void PEImage::DumpExportTable() const {
  if (!IsInitialized()) return;

  if (!directories_[ExportTable].VirtualAddress) return;

  const address_t
    dir_start = base_ + directories_[ExportTable].VirtualAddress,
    dir_end = dir_start + directories_[ExportTable].Size;

  const auto dir_table = load_data<IMAGE_EXPORT_DIRECTORY>(dir_start);
  dprintf("%s\n", RvaString(dir_table.Name).c_str());

  struct ExportTable {
    DWORD entry_;
    std::string name_;
  };
  std::vector<ExportTable> table(dir_table.NumberOfFunctions);
  for (DWORD i = 0; i < dir_table.NumberOfFunctions; ++i) {
    table[i].entry_ =
      load_data<DWORD>(base_ + dir_table.AddressOfFunctions + i * 4);
  }

  for (DWORD i = 0; i < dir_table.NumberOfNames; ++i) {
    const int index = load_data<uint16_t>(
      base_ + dir_table.AddressOfNameOrdinals + i * 2);
    const auto rva_name =
      load_data<DWORD>(base_ + dir_table.AddressOfNames + i * 4);
    table[index].name_ = RvaString(rva_name);
  }

  for (DWORD i = 0; i < dir_table.NumberOfFunctions; ++i) {
    if (!table[i].entry_) continue;

    const bool is_forwarder =
      base_ + table[i].entry_ >= dir_start
      && base_ + table[i].entry_ < dir_end;

    std::stringstream s;
    s << std::dec << std::setw(4) << i
      << ' ' << address_string(base_ + dir_table.AddressOfFunctions + i * 4)
      << (is_forwarder ? " * " : " ")
      << (table[i].name_.size() > 0 ? table[i].name_ : "[NONAME]")
      << ' ';

    if (is_forwarder) {
      // Forwarder RVA
      s << RvaString(table[i].entry_);
    }
    else {
      // Export RVA
      DumpAddressAndSymbol(s, base_ + table[i].entry_);
    }

    dprintf("%s\n", s.str().c_str());
  }
}

#include "exception_handling.h"

static
void DumpScopeTable(std::ostream &s,
                    address_t scope_table,
                    address_t base,
                    address_t exception_pc) {
  address_t addr = scope_table;
  auto count = load_data<uint32_t>(addr);
  addr += sizeof(uint32_t);

  if (count > 100) {
    // Probably this data is not ScopeTable.
    s << "  Too many records (" << count << ")!" << std::endl;
    count = 100;
  }

  for (uint32_t i = 0; i < count; ++i) {
    using RecordType = std::remove_reference<
        decltype(*SCOPE_TABLE_AMD64::ScopeRecord)>::type;

    const auto record = load_data<RecordType>(addr);
    s << "  ScopeRecord[" << std::dec << i << "] "
      << address_string(addr);
    if (exception_pc != 0
        && exception_pc >= base + record.BeginAddress
        && exception_pc < base + record.EndAddress) {
      s << " <<<<";
    }
    s << std::endl
      << "    [ " << address_string(base + record.BeginAddress)
      << ' ' << address_string(base + record.EndAddress)
      << " )"
      << std::endl;

    s << "    Filter:  ";
    DumpAddressAndSymbol(s, base + record.HandlerAddress);
    s << "\n    Handler: ";
    DumpAddressAndSymbol(s, base + record.JumpTarget);
    s << std::endl;
    addr += sizeof(RecordType);
  }
}

static
void DumpUnwindInfo(int index,
                    std::ostream &s,
                    address_t base,
                    const RUNTIME_FUNCTION_AMD64 &entry,
                    address_t exception_pc) {
  address_t addr = base + entry.UnwindData;
  const auto info = load_data<UNWIND_INFO>(addr);

  if (info.Version >= 2) {
    s << "Unsupported UNWIND_INFO::Version: " << info.Version << std::endl;
    return;
  }

  s << "UNWIND_INFO[" << std::dec << index << "] "
    << address_string(addr)
    << " [ "
    << address_string(base + entry.BeginAddress) << ' '
    << address_string(base + entry.EndAddress) << " )" << std::endl
    << "  Version       = " << static_cast<int>(info.Version) << std::endl
    << "  Flags         = " << static_cast<int>(info.Flags) << std::endl
    << "  SizeOfProlog  = " << static_cast<int>(info.SizeOfProlog) << std::endl
    << "  FrameRegister = " << static_cast<int>(info.FrameRegister) << std::endl
    << "  FrameOffset   = " << static_cast<int>(info.FrameOffset) << std::endl;

  addr += offsetof(UNWIND_INFO, UnwindCode);

  for (int i = 0; i < info.CountOfCodes; ++i) {
    const auto unwind = load_data<UNWIND_CODE>(addr + sizeof(UNWIND_CODE) * i);
    s << "  UnwindCode[" << i << "] = "
      << "{CodeOffset:" << static_cast<int>(unwind.CodeOffset)
      << " UnwindOp:" << static_cast<int>(unwind.UnwindOp)
      << " OpInfo:" << static_cast<int>(unwind.OpInfo)
      << "}\n";
  }

  if (info.Flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {
    int n = (info.CountOfCodes + 1) & ~1;
    addr += sizeof(UNWIND_CODE) * n;
    const auto rva_handler = load_data<uint32_t>(addr);
    addr += sizeof(uint32_t);

    s << "  ExceptionHandler = ";
    DumpAddressAndSymbol(s, base + rva_handler);
    s << std::endl
      << "  HandlerData = " << address_string(addr) << std::endl;

    char symbol[1024];
    uint64_t displacement;
    GetSymbol(base + rva_handler, symbol, &displacement);
    if (displacement == 0 && strstr(symbol, "_C_specific_handler")) {
      // If a handler is _C_specific_handler, we know what HandlerData is.
      DumpScopeTable(s, addr, base, exception_pc);
    }
  }
}

void PEImage::DumpExceptionRecords(address_t exception_pc) const {
  if (!IsInitialized()) return;

  if (!Is64bit()) {
    dprintf("Only x64 is supported for now.  Sorry!\n");
    return;
  }

  const address_t
    dir_start = base_ + directories_[ExceptionTable].VirtualAddress,
    dir_end = dir_start + directories_[ExceptionTable].Size;

  int index = 0;
  for (address_t entry_raw = dir_start;
       entry_raw < dir_end;
       entry_raw += sizeof(RUNTIME_FUNCTION_AMD64), ++index) {
    const auto entry = load_data<RUNTIME_FUNCTION_AMD64>(entry_raw);
    if (entry.UnwindData
        && (exception_pc == 0
            || (exception_pc >= base_ + entry.BeginAddress
                && exception_pc < base_ + entry.EndAddress))) {
      std::stringstream s;
      s << '@' << address_string(entry_raw) << std::endl;
      DumpUnwindInfo(index, s, base_, entry, exception_pc);
      dprintf("%s", s.str().c_str());
    }
  }
}

class ResourceId {
  enum class Type {Number, String, Any};
  Type type_;
  uint16_t number_;
  std::wstring string_;

  ResourceId() : type_(Type::Any) {}

public:
  static const ResourceId &Any() {
    static ResourceId any;
    return any;
  }
  ResourceId(uint16_t id)
    : type_(Type::Number), number_(id)
  {}
  ResourceId(const wchar_t *name) {
    if (IS_INTRESOURCE(name)) {
      type_ = Type::Number;
      number_ = static_cast<uint16_t>(
        reinterpret_cast<uintptr_t>(name) & 0xffff);
    }
    else {
      type_ = Type::String;
      string_ = name;
    }
  }

  ResourceId(ResourceId &&other) = default;
  ResourceId &ResourceId::operator=(ResourceId &&other) = default;

  bool operator==(const ResourceId &other) const {
    if (type_ == Type::Any || other.type_ == Type::Any)
      return true;

    if (type_ != other.type_)
      return false;

    switch (type_) {
    case Type::Number:
      return number_ == other.number_;
    case Type::String:
      return string_ == other.string_;
    default:
      return false;
    }
  }

  bool operator!=(const ResourceId &other) const {
    return !(*this == other);
  }
};

using ResourceIterator = std::function<void (const IMAGE_RESOURCE_DATA_ENTRY &)>;

class ResourceDirectory {
  class DirectoryEntry {
    address_t base_;
    IMAGE_RESOURCE_DIRECTORY_ENTRY data_;

    static std::wstring ResDirString(address_t addr) {
      const uint16_t len = load_data<uint16_t>(addr);
      std::wstring ret;
      if (auto buf = new wchar_t[len + 1]) {
        ULONG cb = 0;
        ReadMemory(addr + 2, buf, len * sizeof(wchar_t), &cb);
        buf[len] = 0;
        ret = buf;
        delete [] buf;
      }
      return ret;
    }

  public:
    DirectoryEntry(address_t base)
      : base_(base)
    {}

    void Iterate(address_t addr,
                 const ResourceId &filter,
                 ResourceIterator func) {
      const auto entry = load_data<IMAGE_RESOURCE_DIRECTORY_ENTRY>(addr);
      if (!entry.Name) return;

      const ResourceId id = entry.NameIsString
        ? ResourceId(ResDirString(base_ + entry.NameOffset).c_str())
        : ResourceId(entry.Id);

      if (id != filter) return;

      if (entry.DataIsDirectory) {
        // dprintf("Dir -> %p\n", base_ + entry.OffsetToDirectory);
        ResourceDirectory dir(base_);
        dir.Iterate(base_ + entry.OffsetToDirectory, ResourceId::Any(), func);
      }
      else {
        // dprintf("Data %p\n", base_ + entry.OffsetToData);
        const auto data_entry =
          load_data<IMAGE_RESOURCE_DATA_ENTRY>(base_ + entry.OffsetToData);
        func(data_entry);
      }
    }
  };

  address_t base_;

public:
  ResourceDirectory(address_t base)
    : base_(base)
  {}

  void Iterate(address_t addr,
               const ResourceId &filter,
               ResourceIterator func) {
    const auto fastLookup = load_data<IMAGE_RESOURCE_DIRECTORY>(addr);
    const int num_entries =
      fastLookup.NumberOfNamedEntries + fastLookup.NumberOfIdEntries;
    const address_t first_entry = addr + sizeof(IMAGE_RESOURCE_DIRECTORY);
    for (int i = 0; i < num_entries; ++i) {
      DirectoryEntry entry(base_);
      entry.Iterate(first_entry + i * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY),
                    filter,
                    func);
    }
  }
};

VS_FIXEDFILEINFO PEImage::GetVersion() const {
  VS_FIXEDFILEINFO version{};

  if (!IsInitialized())
    return version;

  const address_t
    dir_start = base_ + directories_[ResourceTable].VirtualAddress,
    dir_end = dir_start + directories_[ResourceTable].Size;

  ResourceDirectory dir(dir_start);
  dir.Iterate(dir_start,
              ResourceId(RT_VERSION),
              [this, &version](const IMAGE_RESOURCE_DATA_ENTRY &data) {
                struct VS_VERSIONINFO {
                    WORD wLength;
                    WORD wValueLength;
                    WORD wType;
                    WCHAR szKey[16]; // L"VS_VERSION_INFO"
                    VS_FIXEDFILEINFO Value;
                };

                if (data.Size < sizeof(VS_VERSIONINFO)) return;

                const auto ver =
                  load_data<VS_VERSIONINFO>(base_ + data.OffsetToData);
                if (ver.wValueLength != sizeof(VS_FIXEDFILEINFO)
                    || ver.Value.dwSignature != 0xFEEF04BD)
                  return;

                version = ver.Value;
              });

  return version;
}

int PEImage::LookupSection(uint32_t rva, uint32_t size) const {
  struct Comparer {
    uint32_t start_, end_;
    Comparer(uint32_t rva, uint32_t size)
      : start_(rva), end_(rva + size) {}
    bool operator()(const IMAGE_SECTION_HEADER &section) const {
      return
        start_ >= section.VirtualAddress
        && end_ <= section.VirtualAddress + section.Misc.VirtualSize;
    }
  };
  auto it = std::find_if(sections_.begin(),
                         sections_.end(),
                         Comparer(rva, size));
  return it == sections_.end() ? -1 : static_cast<int>(it - sections_.begin());
}

void PEImage::DumpSectionTable() const {
  if (!IsInitialized()) return;

  dprintf("Sections:\n");
  for (int i = 0; i < sections_.size(); ++i) {
    std::stringstream s;

    char name[IMAGE_SIZEOF_SHORT_NAME + 1];
    memcpy(name, sections_[i].Name, IMAGE_SIZEOF_SHORT_NAME);
    name[IMAGE_SIZEOF_SHORT_NAME] = 0;
    s << std::setw(3) << i
      << ' ' << std::left << std::setw(9) << name
      << "rva: "
      << std::hex << sections_[i].VirtualAddress
      << '-' << sections_[i].VirtualAddress + sections_[i].Misc.VirtualSize
      << " file: "
      << sections_[i].PointerToRawData
      << '-' << sections_[i].PointerToRawData + sections_[i].SizeOfRawData;

    dprintf("%s\n", s.str().c_str());
  }

  const char *DirNames[]= {
    "Export",
    "Import",
    "Resource",
    "Exception",
    "Certificate",
    "BaseRelocation",
    "DebugInfo",
    "ArchitectureSpecific",
    "GlobalPointerRegister",
    "Tls",
    "LoadConfig",
    "BoundImportTable",
    "IAT",
    "DelayImportDescriptor",
    "CLRHeader",
    "Reserved",
  };

  dprintf("\nDirectories:\n");
  for (int i = 0; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; ++i) {
    std::stringstream s;
    s << std::setw(3) << i
      << ' ' << std::left << std::setw(24) << DirNames[i];

    if (directories_[i].VirtualAddress && directories_[i].Size) {
      s << std::hex << directories_[i].VirtualAddress
        << '-' << directories_[i].VirtualAddress + directories_[i].Size;

      auto index = LookupSection(
        directories_[i].VirtualAddress, directories_[i].Size);
      if (index >= 0) {
        char name[IMAGE_SIZEOF_SHORT_NAME + 2];
        name[0] = ' ';
        memcpy(name + 1, sections_[index].Name, IMAGE_SIZEOF_SHORT_NAME);
        name[IMAGE_SIZEOF_SHORT_NAME + 1] = 0;
        s << name;
      }
    }

    dprintf("%s\n", s.str().c_str());
  }

  dprintf("\n");
}

DECLARE_API(cfg) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (PEImage pe = GetExpression(vargs[0].c_str())) {
      pe.DumpLoadConfig();
    }
  }
}

DECLARE_API(imp) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (PEImage pe = GetExpression(vargs[0].c_str())) {
      pe.DumpIAT(vargs.size() >= 2 ? vargs[1] : std::string());
    }
  }
}

DECLARE_API(ext) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (PEImage pe = GetExpression(vargs[0].c_str())) {
      pe.DumpExportTable();
    }
  }
}

DECLARE_API(sec) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (PEImage pe = GetExpression(vargs[0].c_str())) {
      pe.DumpSectionTable();
    }
  }
}

DECLARE_API(ex) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (PEImage pe = GetExpression(vargs[0].c_str())) {
      pe.DumpExceptionRecords(vargs.size() >= 2
          ? GetExpression(vargs[1].c_str()) : 0);
    }
  }
}

DECLARE_API(ver) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    const auto base = GetExpression(vargs[0].c_str());
    if (PEImage pe = base) {
      const auto ver = pe.GetVersion();
      std::stringstream s;
      s << "ImageBase:       " << address_string(base) << std::endl
        << "File version:    "
          << HIWORD(ver.dwFileVersionMS) << '.'
          << LOWORD(ver.dwFileVersionMS) << '.'
          << HIWORD(ver.dwFileVersionLS) << '.'
          << LOWORD(ver.dwFileVersionLS) << std::endl
        << "Product version: "
          << HIWORD(ver.dwProductVersionMS) << '.'
          << LOWORD(ver.dwProductVersionMS) << '.'
          << HIWORD(ver.dwProductVersionLS) << '.'
          << LOWORD(ver.dwProductVersionLS) << std::endl
        << std::endl;
      dprintf("%s", s.str().c_str());
    }
  }
}
