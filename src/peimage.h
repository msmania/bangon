class PEImage final {
  address_t base_{};
  bool is64bit_{};
  IMAGE_DATA_DIRECTORY directories_[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];

  std::string RvaString(uint32_t offset) const;
  bool Load(ULONG64 ImageBase);
  void DumpIATEntries(int index,
                      std::ostream &s,
                      const IMAGE_IMPORT_DESCRIPTOR &desc) const;

public:
  PEImage(address_t base);

  bool IsInitialized() const;
  bool Is64bit() const;

  void DumpIAT(const std::string &target) const;
  VS_FIXEDFILEINFO GetVersion() const;
};