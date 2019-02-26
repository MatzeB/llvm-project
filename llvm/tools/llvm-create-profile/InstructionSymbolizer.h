//===-- InstructionSymbolizer.h - Wrapper around llvm symbolization services *-
// C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of InstructionSymbolizer which provide an
/// interface to llvm::symbolization services
/// facebook T26943842
//===----------------------------------------------------------------------------------===//
#ifndef LLVM_INSTRUCTIONSYMBOLIZER_H
#define LLVM_INSTRUCTIONSYMBOLIZER_H

#include "PerfSampleReader.h"
#include "SymbolLoader.h"
#include "llvm/DebugInfo/Symbolize/DIPrinter.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Path.h"

#define DEBUG_TYPE "autofdo_symbolizer"
namespace autofdo {

using llvm::DIInliningInfo;
using llvm::DILineInfo;
using llvm::DILocation;
using llvm::Expected;
using llvm::object::Binary;
using llvm::symbolize::LLVMSymbolizer;

class InstructionSymbolizer {

public:
  explicit InstructionSymbolizer() {
    SymbolizerOption.PrintFunctions =
        DILineInfoSpecifier::FunctionNameKind::LinkageName;
    SymbolizerOption.UseSymbolTable = false;
    SymbolizerOption.Demangle = false;
    SymbolizerOption.RelativeAddresses = false;
    Symbolizer.reset(new llvm::symbolize::LLVMSymbolizer(SymbolizerOption));
  };

  /// \param inst InstructionLocation to symbolize
  /// \return full inlining information, or empty results in case of errors
  DIInliningInfo symbolizeInstruction(const InstructionLocation &inst) {
    LLVM_DEBUG(std::cerr << "Symbolizing Instruction " << std::hex << inst
                         << std::dec << std::endl);
    Optional<object::SectionedAddress> expected_vaddr =
        getVaddressFromFileOffset(inst);
    if (!expected_vaddr)
      return DIInliningInfo();
    LLVM_DEBUG(std::cerr << "saddr : " << expected_vaddr->Address << endl);
    Expected<DIInliningInfo> ret =
        Symbolizer->symbolizeInlinedCode(inst.objectFile, *expected_vaddr);
    if ((!ret) or ((ret.get().getNumberOfFrames() > 0) and
                   ((ret.get().getFrame(0).FunctionName == "<invalid>"))))
      return DIInliningInfo();
    return ret.get();
  }

  static uint32_t getDuplicationFactor(const DILineInfo &lineInfo) {
    return llvm::DILocation::getDuplicationFactorFromDiscriminator(
        lineInfo.Discriminator);
  }

  // TODO: the offset encoding should probably be moved closer to the
  // profile_writer
  /// Encode the line offset and the discriminator into a uint32_t
  /// \params lineInfo to encode
  static uint32_t
  getOffset(const DILineInfo &lineInfo
            /*bool use_discriminator_encoding :we always use the encoding*/) {
    // TODO should we assert that line - start_line < 2^16?
    return ((lineInfo.Line - lineInfo.StartLine) << 16) |
           llvm::DILocation::getBaseDiscriminatorFromDiscriminator(
               lineInfo.Discriminator);
  }

  /// Compute the virtual address corresponding to a InstructionLocation
  /// \return virtual address or error in case the virtual address cannot be
  /// computed
  llvm::Optional<object::SectionedAddress>
  getVaddressFromFileOffset(const InstructionLocation &loc) {
    return object::SectionedAddress{loc.offset,
                                    object::SectionedAddress::UndefSection};
  }

  // Debugging methods
  void print(const DILineInfo &info, std::ostream &out) {
    out << std::string(llvm::sys::path::filename(info.FileName)) << ":"
        << info.StartLine << "-" << info.Line << ":" << info.Discriminator;
  }
  void print(const DIInliningInfo &info, std::ostream &out) {
    std::string str;
    llvm::raw_string_ostream stream(str);
    llvm::symbolize::PrinterConfig Config;
    Config.PrintFunctions = true;
    llvm::symbolize::LLVMPrinter Printer(stream, errs(), Config);
    Printer.print({"", 0}, info);
    out << stream.str();
  }

private:
  /// cache symbolization results
  std::map<InstructionLocation, std::unique_ptr<Expected<DIInliningInfo>>>
      InstructionMap;
  llvm::symbolize::LLVMSymbolizer::Options SymbolizerOption;
  mutable std::unique_ptr<llvm::symbolize::LLVMSymbolizer> Symbolizer;
};

} // namespace autofdo
#undef DEBUG_TYPE
#endif // LLVM_INSTRUCTIONSYMBOLIZER_H
