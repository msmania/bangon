#include <algorithm>
#include <functional>
#include <vector>
#define KDEXT_64BIT
#include <windows.h>
#include <wdbgexts.h>

#include "common.h"

class Tree {
  address_t addr_;

  void PreOrderInternal(int level,
                        std::function<void(uint32_t, address_t)> callback) const {
    if (!addr_) return;

    callback(level, addr_);

    static const auto
      offsetL = get_field_offset("nt!_RTL_BALANCED_NODE", "Left"),
      offsetR = get_field_offset("nt!_RTL_BALANCED_NODE", "Right");

    Tree left(load_pointer(addr_ + offsetL));
    if (left) left.PreOrderInternal(level + 1, callback);

    Tree right(load_pointer(addr_ + offsetR));
    if (right) right.PreOrderInternal(level + 1, callback);
  }

public:
  struct Node {
    uint32_t level_;
    address_t addr_;
    Node(uint32_t level, address_t addr)
      : level_(level), addr_(addr)
    {}
  };

  Tree(address_t addr) : addr_(addr) {}

  operator bool() const {
    return !!addr_;
  }

  void MoveToRoot() {
    static const auto offsetParent =
      get_field_offset("nt!_RTL_BALANCED_NODE", "ParentValue");

    for (address_t p = addr_;;) {
      p = load_pointer(p + offsetParent) & ~7;
      if (!p) break;
      addr_ = p;
    }
  }

  void PreOrder(std::function<void(uint32_t, address_t)> callback) const {
    PreOrderInternal(0, callback);
  }
};

class UnicodeString final {
  uint16_t Length = 0;
  uint16_t MaximumLength = 0;
  uint8_t *Buffer = nullptr;

public:
  UnicodeString(address_t addr) {
    static const auto
      offsetLen = get_field_offset("nt!_UNICODE_STRING", "Length"),
      offsetMaxLen = get_field_offset("nt!_UNICODE_STRING", "MaximumLength"),
      offsetBuf = get_field_offset("nt!_UNICODE_STRING", "Buffer");

    Length = load_data<uint16_t>(addr + offsetLen);
    MaximumLength = load_data<uint16_t>(addr + offsetMaxLen);

    Buffer = new uint8_t[MaximumLength + sizeof(wchar_t)];

    ULONG bytesRead;
    ReadMemory(load_pointer(addr + offsetBuf),
               Buffer,
               MaximumLength,
               &bytesRead);

    Buffer[bytesRead] = Buffer[bytesRead + 1] = 0;
  }

  ~UnicodeString() {
    if (Buffer) delete [] Buffer;
  }

  const wchar_t *c_str() const {
    return reinterpret_cast<wchar_t*>(Buffer);
  }
};

class VadNode {
  address_t addr_;

  address_t controlArea_ = 0;
  address_t startAddr_ = 0, endAddr_ = 0;

public:
  VadNode(address_t addr) : addr_(addr) {
    static const auto
      offsetCore = get_field_offset("nt!_MMVAD", "Core"),
      offsetStartL = get_field_offset("nt!_MMVAD_SHORT", "StartingVpn"),
      offsetStartH = get_field_offset("nt!_MMVAD_SHORT", "StartingVpnHigh"),
      offsetEndL = get_field_offset("nt!_MMVAD_SHORT", "EndingVpn"),
      offsetEndH = get_field_offset("nt!_MMVAD_SHORT", "EndingVpnHigh"),
      offsetSubsection = get_field_offset("nt!_MMVAD", "Subsection"),
      offsetControlArea = get_field_offset("nt!_SUBSECTION", "ControlArea");

    if (auto subSection = load_pointer(addr_ + offsetSubsection)) {
      controlArea_ = load_pointer(subSection + offsetControlArea);
    }

    address_t core = addr_ + offsetCore;
    startAddr_ =
      (static_cast<address_t>(load_data<uint8_t>(core + offsetStartH)) << 32)
      | load_data<uint32_t>(core + offsetStartL);
    startAddr_ <<= 12;
    endAddr_ =
      (static_cast<address_t>(load_data<uint8_t>(core + offsetEndH)) << 32)
      | load_data<uint32_t>(core + offsetEndL);
    ++endAddr_ ;
    endAddr_ <<= 12;
  }

  operator bool() const {
    return !!addr_;
  }

  void Dump() const {
    if (!addr_) return;

    address_string addr(addr_), start(startAddr_), end(endAddr_);
    dprintf("%s %s %s", addr, start, end);

    if (controlArea_) {
      static const auto
        offsetFlags = get_field_offset("nt!_CONTROL_AREA", "u"),
        offsetFilePointer = get_field_offset("nt!_CONTROL_AREA", "FilePointer"),
        offsetFileName = get_field_offset("nt!_FILE_OBJECT", "FileName");

      auto flags = load_data<uint32_t>(controlArea_ + offsetFlags);
      if (flags & 0x80) {
        if (auto fileobj = load_pointer(controlArea_ + offsetFilePointer) & ~0xf) {
          address_string fileobjAddr(fileobj);
          UnicodeString filename(fileobj + offsetFileName);
          dprintf(" %s %ls", fileobjAddr, filename.c_str());
        }
      }
    }

    dprintf("\n");
  }

  static bool CompareStartVpn(const VadNode &node1, const VadNode &node2) {
    return node1.startAddr_ < node2.startAddr_;
  }
};

class EProcess final {
  address_t base_;

public:
  EProcess(address_t base) : base_(base) {}

  operator bool() const {
    return !!base_;
  }

  void DumpVAD() const {
    if (!base_) return;

    auto vadRoot =
      load_pointer(base_ + get_field_offset("nt!_EPROCESS", "VadRoot"));
    if (!vadRoot) return;

    vadRoot =
      load_pointer(vadRoot + get_field_offset("nt!_RTL_AVL_TREE", "Root"));
    if (!vadRoot) return;

    Tree tree(vadRoot);
    tree.MoveToRoot();

    std::vector<VadNode> nodes;

    tree.PreOrder(
      [&nodes](uint32_t level, address_t addr) {
        nodes.emplace_back(addr);
      }
    );
    std::sort(nodes.begin(), nodes.end(), VadNode::CompareStartVpn);
    for (size_t i = 0; i < nodes.size(); ++i) {
      dprintf("%4d ", i);
      nodes[i].Dump();
    }
  }
};

DECLARE_API(ntvad) {
  const auto vargs = get_args(args);
  if (vargs.size() > 0) {
    if (EProcess eproc = GetExpression(vargs[0].c_str())) {
      eproc.DumpVAD();
    }
  }
}
