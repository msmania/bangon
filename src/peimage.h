#pragma once

class PEImage final {
public:
  using BoundDirT = std::unordered_map<std::string, DWORD>;

private:
  address_t base_{};
  bool is64bit_{};
  IMAGE_DATA_DIRECTORY directories_[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
  std::vector<IMAGE_SECTION_HEADER> sections_;

  bool Load(ULONG64 ImageBase);
  void DumpIATEntries(int index,
                      std::ostream &s,
                      const IMAGE_IMPORT_DESCRIPTOR &desc) const;
  int LookupSection(uint32_t rva, uint32_t size) const;
  BoundDirT LoadBoundImportDirectory() const;

public:
  PEImage(address_t base);

  operator bool() const;

  bool IsInitialized() const;
  bool Is64bit() const;

  void DumpIAT(const std::string &target) const;
  void DumpLoadConfig() const;
  void DumpExportTable() const;
  void DumpExceptionRecords(address_t exception_pc) const;
  void DumpSectionTable() const;
  VS_FIXEDFILEINFO GetVersion() const;
};