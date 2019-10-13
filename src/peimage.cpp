#include <sstream>
#include <functional>
#include <iomanip>
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
  switch (fileHeader.Machine) {
    default:
      dprintf("Unsupported platform - %04x.\n", fileHeader.Machine);
      return false;
    case IMAGE_FILE_MACHINE_AMD64: {
      const auto optHeader = load_data<IMAGE_OPTIONAL_HEADER64>(rvaOptHeader);
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
  char symbol[100];
  ULONG64 displacement;

  for (int index_entry = 0; ; ++index_entry) {
    const address_t
      rva_name = load_pointer(start_name + index_entry * address_size),
      rva_func = load_pointer(start_func + index_entry * address_size);
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

    GetSymbol(rva_func, symbol, &displacement);
    s << ' ' << address_string(rva_func) << ' ' << symbol;
    if (displacement) s << '+' << displacement;
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

DECLARE_API(imp) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    PEImage pe(GetExpression(vargs[0].c_str()));
    if (pe.IsInitialized()) {
      pe.DumpIAT(vargs.size() >= 2 ? vargs[1] : std::string());
    }
  }
}

DECLARE_API(ver) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    const auto base = GetExpression(vargs[0].c_str());
    PEImage pe(base);
    if (pe.IsInitialized()) {
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
