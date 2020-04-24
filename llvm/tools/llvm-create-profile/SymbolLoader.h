//
// Created by kaderkeita on 5/2/18.
// facebook T26943842

#ifndef LLVM_SYMBOLLOADER_H
#define LLVM_SYMBOLLOADER_H

#include "SampleReader.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Casting.h"
#include <iostream>

#define DEBUG_TYPE "file_loader"
namespace autofdo {
using llvm::object::Binary;
using llvm::object::ELF32LEObjectFile;
using llvm::object::ELF64LEObjectFile;
using llvm::object::ELFObjectFileBase;
using llvm::object::ObjectFile;
using llvm::object::OwningBinary;
class SymbolLoader {

public:
  struct ElfSymbol {
    InstructionLocation StartAddress;
    std::string Name;
    size_t Size;
    uint32_t GroupId;
    Range range() const {
      InstructionLocation EndAddress = StartAddress;
      EndAddress.offset += Size;
      return {StartAddress, EndAddress};
    }
  };

  static void
  loadSymbolFromSymbolTable(const std::string &binary_path,
                            std::vector<std::vector<ElfSymbol>> &symbolGroups) {
    auto binary_file = openELFFile(binary_path);
    auto &elffile = *llvm::dyn_cast<llvm::object::ELFObjectFileBase>(
        binary_file.getBinary());
    for (auto &symb : elffile.symbols()) {
      auto type = symb.getType();
      if (!type) {
        (void)type.takeError();
        continue;
      }
      if (type.get() != llvm::object::SymbolRef::Type::ST_Function)
        continue;
      auto name = symb.getName();
      auto address = symb.getAddress();
      auto section = symb.getSection();
      if (!name or !address or !section) {
        (void)name.takeError();
        (void)address.takeError();
        (void)section.takeError();
        std::cerr << "Can't get symbol infos" << std::endl;
        continue;
      }

      if (symb.getSize() == 0) {
        continue;
      }

      auto symbolVirtualAddress = address.get();
      LLVM_DEBUG(std::cout << std::hex << "Adding symbol " << name.get().str()
                           << ", at address " << address.get()
                           << ", section offset : " << std::dec << std::endl);

      symbolGroups.emplace_back(std::vector<ElfSymbol>());
      symbolGroups.back().emplace_back(ElfSymbol{
          InstructionLocation{binary_path, symbolVirtualAddress},
          name.get().str(), symb.getSize(), (uint32_t)symbolGroups.size()});
    }
  }

  /// Open file from a disk and fail terminally is the fileformat is not
  /// supported \param binary_path \return opened File
  static llvm::object::OwningBinary<Binary>
  openELFFile(const std::string &binary_path) {
    auto expected_file = llvm::object::createBinary(binary_path);
    if (!expected_file) {
      llvm::errs() << "Couldn't open " << binary_path << "\n";
      exit(-1);
    }

    auto binary_file = expected_file.get().getBinary();

    if (auto efl64 = llvm::dyn_cast<ELF64LEObjectFile>(binary_file)) {
      if (efl64->getELFFile().getHeader().e_type != llvm::ELF::ET_EXEC and
          efl64->getELFFile().getHeader().e_type != llvm::ELF::ET_DYN) {
        std::cerr << "Couldnt open " << binary_path
                  << "support executable or .so only \n";
        exit(-1);
      }
    } else if (auto efl32 = llvm::dyn_cast<ELF32LEObjectFile>(binary_file)) {
      if (efl32->getELFFile().getHeader().e_type != llvm::ELF::ET_EXEC and
          efl32->getELFFile().getHeader().e_type != llvm::ELF::ET_DYN) {
        std::cerr << "Couldnt open " << binary_path
                  << "support executable or .so only \n";
        exit(-1);
      }
    } else {
      std::cerr << "Unsupported file format " << binary_path << std::endl;
    }

    return std::move(expected_file.get());
  }

  static void
  loadSymbolFromDebugTable(const std::string &binary_path,
                           std::vector<std::vector<ElfSymbol>> &symbolGroups) {
    auto binary_file = openELFFile(binary_path);
    auto &object_file = *llvm::dyn_cast<ObjectFile>(binary_file.getBinary());
    auto debugContext = llvm::DWARFContext::create(object_file);
    if (debugContext == nullptr) {
      std::cerr << "Misssing debug info " << binary_path << std::endl;
      return;
    }

    for (const auto &compilation_unit : debugContext->compile_units()) {
      for (const auto &dieInfo : compilation_unit->dies()) {
        llvm::DWARFDie die(compilation_unit.get(), &dieInfo);

        if (!die.isSubprogramDIE())
          continue;
        auto Name = die.getName(llvm::DINameKind::LinkageName);
        if (!Name)
          Name = die.getName(llvm::DINameKind::ShortName);
        if (!Name)
          continue;

        auto RangesOrError = die.getAddressRanges();
        if (!RangesOrError)
          continue;
        const llvm::DWARFAddressRangesVector &ranges = RangesOrError.get();

        if (ranges.empty())
          continue;

        // A function may be spilt into multiple non-continuous address ranges.
        // Map each range to a standalone symbol and group the ranges by
        // function names. This allows sample counts be processed independently
        // for each range. Profiles of ranges with the same function name will
        // be merged finally.
        symbolGroups.emplace_back(std::vector<ElfSymbol>());
        for (const auto &range : ranges) {
          uint64_t functionStart = range.LowPC;
          uint64_t functionSize = range.HighPC - functionStart;

          LLVM_DEBUG(std::cout << std::hex << "Symbol for debug section "
                               << std::string(Name)
                               << ", symbol offset : " << functionStart
                               << std::dec << ", symbol size : " << functionSize
                               << std::endl);

          symbolGroups.back().emplace_back(ElfSymbol{
              InstructionLocation{binary_path, (uint64_t)functionStart},
              std::string(Name), functionSize, (uint32_t)symbolGroups.size()});
        }
      }
    }
  }

  static uint64_t preferedBasedAddress(const std::string &binary_path) {
    auto binary_file = openELFFile(binary_path);
    if (llvm::dyn_cast<ELF64LEObjectFile>(binary_file.getBinary())) {
      return preferedBasedAddress<ELF64LEObjectFile>(*binary_file.getBinary());
    } else if (llvm::dyn_cast<ELF32LEObjectFile>(binary_file.getBinary())) {
      return preferedBasedAddress<ELF32LEObjectFile>(*binary_file.getBinary());
    }
    std::cerr << "Unexpected errors " << std::endl;
    exit(-1);
  }
  /// Return the prefered load address of the binary, usually the load address
  /// of the first loadble segment \param binary \return load address of the
  /// loadbable segment
  template <typename FILETYPE>
  static uint64_t preferedBasedAddress(llvm::object::Binary &binary) {
    FILETYPE &elffile = *llvm::dyn_cast<FILETYPE>(&binary);
    auto headers = elffile.getELFFile().program_headers();
    if (!headers) {
      std::cerr << "Fail to load segment information for "
                << std::string(binary.getFileName()) << std::endl;
      exit(-1);
    }

    for (auto header : headers.get()) {
      if (header.p_type == llvm::ELF::PT_LOAD)
        return header.p_vaddr;
    }
    std::cerr << "No load segment  : " << std::string(binary.getFileName())
              << std::endl;
    exit(-1);
  }
};
} // namespace autofdo
#undef DEBUG_TYPE
#endif // LLVM_SYMBOLLOADER_H
