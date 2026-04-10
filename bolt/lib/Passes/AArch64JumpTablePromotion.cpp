//===- bolt/Passes/AArch64JumpTablePromotion.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Passes/AArch64JumpTablePromotion.h"
#include "AArch64InstrInfo.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/CommandLine.h"

#define DEBUG_TYPE "bolt"

using namespace llvm;

namespace opts {
extern cl::opt<unsigned> Verbosity;
}

namespace llvm {
namespace bolt {

namespace {

struct AArch64JTSiteInfo {
  const BinaryContext::AArch64JumpTableInfo &JTInfo;
  BinaryFunction &BF;
  BinaryBasicBlock &BranchBB;
  MCInst &AdrInst;
  MCInst *AdrAddInst;
  MCInst &LoadInst;
  MCInst &AddInst;
  MCInst &BranchInst;
  SmallVector<std::pair<MCInst *, uint64_t>, 2> ReferenceInsts;
};

static bool isReg64(const MCRegisterInfo &MRI, MCPhysReg Reg) {
  return MRI.getRegClass(AArch64::GPR64RegClassID).contains(Reg);
}

static std::optional<uint64_t>
translateInputAddressToOutputOffset(const BinaryFunction &BF,
                                    uint64_t InputAddress) {
  if (InputAddress < BF.getAddress() ||
      InputAddress > BF.getAddress() + BF.getSize())
    return std::nullopt;

  const uint64_t InputOffset = InputAddress - BF.getAddress();
  const BinaryBasicBlock *BB = BF.getBasicBlockContainingOffset(InputOffset);
  if (!BB) {
    if (InputOffset == BF.getSize()) {
      if (!BF.getLayout().block_empty())
        return BF.getLayout().block_back()->getOutputEndAddress();
      return uint64_t(0);
    }
    return std::nullopt;
  }

  return std::min(BB->getOutputStartAddress() + InputOffset - BB->getOffset(),
                  BB->getOutputEndAddress());
}

static std::optional<uint64_t>
resolveSymbolOutputOffset(const BinaryFunction &BF, const MCSymbol &Symbol) {
  if (const BinaryBasicBlock *BB = BF.getBasicBlockForLabel(&Symbol))
    return BB->getOutputStartAddress();

  if (&Symbol == BF.getSymbol())
    return uint64_t(0);

  if (&Symbol == BF.getFunctionEndLabel()) {
    if (!BF.getLayout().block_empty())
      return BF.getLayout().block_back()->getOutputEndAddress();
    return uint64_t(0);
  }

  for (const BinaryBasicBlock &BB : BF) {
    if (BF.getSecondaryEntryPointSymbol(BB) == &Symbol)
      return BB.getOutputStartAddress();
  }

  const BinaryContext &BC = BF.getBinaryContext();
  if (ErrorOr<uint64_t> SymbolAddress = BC.getSymbolValue(Symbol))
    return translateInputAddressToOutputOffset(BF, *SymbolAddress);

  return std::nullopt;
}

static std::optional<FragmentNum>
resolveSymbolOutputFragment(const BinaryFunction &BF, const MCSymbol &Symbol) {
  if (const BinaryBasicBlock *BB = BF.getBasicBlockForLabel(&Symbol))
    return BB->getFragmentNum();

  if (&Symbol == BF.getSymbol())
    return FragmentNum::main();

  if (&Symbol == BF.getFunctionEndLabel()) {
    if (!BF.getLayout().block_empty())
      return BF.getLayout().block_back()->getFragmentNum();
    return FragmentNum::main();
  }

  for (const BinaryBasicBlock &BB : BF) {
    if (BF.getSecondaryEntryPointSymbol(BB) == &Symbol)
      return BB.getFragmentNum();
  }

  const BinaryContext &BC = BF.getBinaryContext();
  if (ErrorOr<uint64_t> SymbolAddress = BC.getSymbolValue(Symbol)) {
    if (*SymbolAddress < BF.getAddress() ||
        *SymbolAddress > BF.getAddress() + BF.getSize())
      return std::nullopt;

    const uint64_t InputOffset = *SymbolAddress - BF.getAddress();
    if (const BinaryBasicBlock *BB = BF.getBasicBlockContainingOffset(InputOffset))
      return BB->getFragmentNum();

    if (InputOffset == BF.getSize() && !BF.getLayout().block_empty())
      return BF.getLayout().block_back()->getFragmentNum();
  }

  return std::nullopt;
}

static bool canEncodeAArch64RelativeInOutput(const BinaryFunction &BF,
                                             const MCSymbol &BaseSymbol,
                                             ArrayRef<MCSymbol *> Targets) {
  std::optional<FragmentNum> BaseFragment =
      resolveSymbolOutputFragment(BF, BaseSymbol);
  if (!BaseFragment)
    return false;

  return llvm::all_of(Targets, [&](const MCSymbol *Target) {
    std::optional<FragmentNum> TargetFragment =
        resolveSymbolOutputFragment(BF, *Target);
    return TargetFragment && *TargetFragment == *BaseFragment;
  });
}

static bool fitsAArch64JTType(JumpTable::JumpTableType Type,
                              ArrayRef<int64_t> Deltas) {
  unsigned ScaledBits = 0;
  bool IsSigned = false;
  uint64_t MaxUnsigned = 0;
  switch (Type) {
  case JumpTable::JTT_AARCH64_I8_X4:
    ScaledBits = 8;
    IsSigned = true;
    break;
  case JumpTable::JTT_AARCH64_U8_X4:
    ScaledBits = 8;
    MaxUnsigned = std::numeric_limits<uint8_t>::max();
    break;
  case JumpTable::JTT_AARCH64_I16_X4:
    ScaledBits = 16;
    IsSigned = true;
    break;
  case JumpTable::JTT_AARCH64_U16_X4:
    ScaledBits = 16;
    MaxUnsigned = std::numeric_limits<uint16_t>::max();
    break;
  case JumpTable::JTT_AARCH64_U32_X4:
    ScaledBits = 32;
    MaxUnsigned = std::numeric_limits<uint32_t>::max();
    break;
  case JumpTable::JTT_AARCH64_I32: {
    for (int64_t Delta : Deltas) {
      if (!isInt<32>(Delta))
        return false;
    }
    return true;
  }
  case JumpTable::JTT_NORMAL:
    return true;
  default:
    return false;
  }

  for (int64_t Delta : Deltas) {
    if (Delta & 0x3)
      return false;

    const int64_t ScaledDelta = Delta / 4;
    if (IsSigned) {
      if ((ScaledBits == 8 && !isInt<8>(ScaledDelta)) ||
          (ScaledBits == 16 && !isInt<16>(ScaledDelta)))
        return false;
    } else {
      if (ScaledDelta < 0 || (uint64_t)ScaledDelta > MaxUnsigned)
        return false;
    }
  }
  return true;
}

static std::optional<unsigned>
getAArch64JTLoadOpcode(JumpTable::JumpTableType Type, bool IndexIs64) {
  switch (Type) {
  case JumpTable::JTT_AARCH64_I8_X4:
  case JumpTable::JTT_AARCH64_U8_X4:
    return IndexIs64 ? AArch64::LDRBBroX : AArch64::LDRBBroW;
  case JumpTable::JTT_AARCH64_I16_X4:
  case JumpTable::JTT_AARCH64_U16_X4:
    return IndexIs64 ? AArch64::LDRHHroX : AArch64::LDRHHroW;
  case JumpTable::JTT_AARCH64_U32_X4:
    return IndexIs64 ? AArch64::LDRWroX : AArch64::LDRWroW;
  case JumpTable::JTT_AARCH64_I32:
    return IndexIs64 ? AArch64::LDRSWroX : AArch64::LDRSWroW;
  case JumpTable::JTT_NORMAL:
    return IndexIs64 ? AArch64::LDRXroX : AArch64::LDRXroW;
  default:
    return std::nullopt;
  }
}

static bool matchesAArch64JTLoadOpcode(const BinaryContext &BC,
                                       const AArch64JTSiteInfo &Site,
                                       JumpTable::JumpTableType Type) {
  if (MCPlus::getNumPrimeOperands(Site.LoadInst) < 3 ||
      !Site.LoadInst.getOperand(2).isReg())
    return false;

  const bool IndexIs64 =
      isReg64(*BC.MRI, Site.LoadInst.getOperand(2).getReg());
  std::optional<unsigned> ExpectedOpcode =
      getAArch64JTLoadOpcode(Type, IndexIs64);
  return ExpectedOpcode && Site.LoadInst.getOpcode() == *ExpectedOpcode;
}

static bool isSupportedAArch64JTType(JumpTable::JumpTableType Type) {
  return Type == JumpTable::JTT_AARCH64_I8_X4 ||
         Type == JumpTable::JTT_AARCH64_U8_X4 ||
         Type == JumpTable::JTT_AARCH64_I16_X4 ||
         Type == JumpTable::JTT_AARCH64_U16_X4 ||
         Type == JumpTable::JTT_AARCH64_I32 ||
         Type == JumpTable::JTT_AARCH64_U32_X4 ||
         Type == JumpTable::JTT_NORMAL;
}

static BinaryBasicBlock::iterator findInstructionIterator(BinaryBasicBlock &BB,
                                                          MCInst *Inst) {
  return llvm::find_if(BB, [&](MCInst &Candidate) {
    return &Candidate == Inst;
  });
}

static BinaryBasicBlock *findInstructionBB(BinaryFunction &BF, MCInst *Inst) {
  if (!Inst)
    return nullptr;

  for (BinaryBasicBlock &BB : BF) {
    if (findInstructionIterator(BB, Inst) != BB.end())
      return &BB;
  }

  return nullptr;
}

static MCInst *findInstructionAtAddress(BinaryFunction &BF, uint64_t Address,
                                        BinaryBasicBlock **BB = nullptr) {
  if (Address < BF.getAddress() || Address > BF.getAddress() + BF.getSize())
    return nullptr;

  const uint64_t Offset = Address - BF.getAddress();
  MCInst *Inst = BF.getInstructionAtOffset(Offset);
  if (!Inst) {
    const BinaryContext &BC = BF.getBinaryContext();
    constexpr uint32_t InvalidOffset = std::numeric_limits<uint32_t>::max();
    for (BinaryBasicBlock &CandidateBB : BF) {
      auto II = llvm::find_if(CandidateBB, [&](MCInst &CandidateInst) {
        return BC.MIB->getOffsetWithDefault(CandidateInst, InvalidOffset) ==
               Offset;
      });
      if (II == CandidateBB.end())
        continue;
      Inst = &*II;
      break;
    }
  }
  if (BB)
    *BB = findInstructionBB(BF, Inst);
  return Inst;
}

static const MCExpr *buildSymbolRefExpr(const MCSymbol *Symbol, int64_t Addend,
                                        MCContext &Ctx) {
  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, Ctx);
  if (!Addend)
    return Expr;

  return MCBinaryExpr::createAdd(Expr, MCConstantExpr::create(Addend, Ctx),
                                 Ctx);
}

static const MCExpr *wrapTargetExprLike(const MCExpr *Template,
                                        const MCExpr *Target, MCContext &Ctx) {
  if (const auto *SpecExpr = dyn_cast<MCSpecifierExpr>(Template)) {
    return MCSpecifierExpr::create(
        wrapTargetExprLike(SpecExpr->getSubExpr(), Target, Ctx),
        SpecExpr->getSpecifier(), Ctx);
  }

  return Target;
}

static bool rewriteJTReference(BinaryContext &BC, const JumpTable &JT,
                               MCInst &Inst) {
  const MCSymbol *JTLabel = JT.getFirstLabel();
  bool Rewritten = false;
  for (MCOperand &Operand : Inst) {
    if (!Operand.isExpr())
      continue;

    const MCSymbol *TargetSym = BC.MIB->getTargetSymbol(Operand.getExpr());
    if (!TargetSym)
      continue;

    ErrorOr<uint64_t> TargetSymAddress = BC.getSymbolValue(*TargetSym);
    if (!TargetSymAddress)
      continue;

    if (*TargetSymAddress < JT.getAddress() ||
        *TargetSymAddress >= JT.getAddress() + JT.getSize()) {
      if (!(JT.getSize() == 0 && *TargetSymAddress == JT.getAddress()))
        continue;
    }

    const int64_t NewAddend =
        static_cast<int64_t>(*TargetSymAddress - JT.getAddress()) +
        BC.MIB->getTargetAddend(Operand.getExpr());
    const MCExpr *NewExpr =
        wrapTargetExprLike(Operand.getExpr(),
                           buildSymbolRefExpr(JTLabel, NewAddend, *BC.Ctx),
                           *BC.Ctx);
    Operand = MCOperand::createExpr(NewExpr);
    Rewritten = true;
  }

  return Rewritten;
}

static void warnJTInfoMismatch(BinaryContext &BC, const JumpTable &JT,
                               const BinaryContext::AArch64JumpTableInfo &Info,
                               const Twine &Reason,
                               const BinaryFunction *BF = nullptr,
                               const MCInst *Inst = nullptr,
                               uint64_t InstAddress = 0) {
  raw_ostream &OS = BC.errs();
  OS << "BOLT-WARN: AArch64 jump table info mismatch for "
     << JT.getFirstLabel()->getName() << " (jt=0x"
     << Twine::utohexstr(JT.getAddress()) << ", type="
     << JumpTable::jumpTableTypeName(JT.Type);
  if (Info.JTType != JT.Type)
    OS << ", info-type=" << JumpTable::jumpTableTypeName(Info.JTType);
  OS << "): " << Reason << '\n';
  if (BF)
    OS << "BOLT-WARN:   function: " << *BF << '\n';
  if (Inst) {
    OS << "BOLT-WARN:   instruction: ";
    BC.printInstruction(OS, *Inst, InstAddress, BF, false, false, false, "");
    OS << '\n';
  }
}

static bool collectJTSiteInfo(BinaryContext &BC, const JumpTable &JT,
                              SmallVectorImpl<AArch64JTSiteInfo> &Sites) {
  auto findBranchByAnnotation =
      [&](BinaryFunction &BF,
          uint64_t JTAddress) -> std::pair<BinaryBasicBlock *, MCInst *> {
    for (BinaryBasicBlock &BB : BF) {
      for (MCInst &Inst : BB) {
        if (!BC.MIB->isIndirectBranch(Inst))
          continue;
        if (BC.MIB->getJumpTable(Inst) != JTAddress)
          continue;
        return {&BB, &Inst};
      }
    }
    return {nullptr, nullptr};
  };

  for (const auto &KV : BC.JumpTableInfos) {
    const BinaryContext::AArch64JumpTableInfo &Info = KV.second;
    if (Info.JTAddress != JT.getAddress())
      continue;

    BinaryFunction *BF =
        BC.getBinaryFunctionContainingAddress(Info.BranchAddress);
    if (!BF) {
      warnJTInfoMismatch(BC, JT, Info, "branch address is not in a "
                                       "disassembled function");
      return false;
    }
    if (BF->getState() != BinaryFunction::State::CFG)
      return false;

    BinaryBasicBlock *BranchBB = nullptr;
    MCInst *BranchInst =
        findInstructionAtAddress(*BF, Info.BranchAddress, &BranchBB);
    if (!BranchInst || !BranchBB ||
        BC.MIB->getJumpTable(*BranchInst) != JT.getAddress()) {
      std::tie(BranchBB, BranchInst) =
          findBranchByAnnotation(*BF, JT.getAddress());
    }
    if (!BranchInst || !BranchBB) {
      warnJTInfoMismatch(BC, JT, Info, "branch sequence was not found", BF,
                         findInstructionAtAddress(*BF, Info.BranchAddress),
                         Info.BranchAddress);
      return false;
    }

    auto BranchII = findInstructionIterator(*BranchBB, BranchInst);
    if (BranchII == BranchBB->end()) {
      warnJTInfoMismatch(BC, JT, Info,
                         "branch instruction was not found in its basic block",
                         BF, BranchInst, Info.BranchAddress);
      return false;
    }

    BinaryBasicBlock *MetadataAddBB = nullptr;
    MCInst *MetadataAddInst =
        findInstructionAtAddress(*BF, Info.AddAddress, &MetadataAddBB);
    BinaryBasicBlock *AddBB = MetadataAddBB;
    MCInst *AddInst = MetadataAddInst;
    if (!AddInst || !AddBB) {
      warnJTInfoMismatch(BC, JT, Info, "add instruction was not found", BF,
                         MetadataAddInst, Info.AddAddress);
      return false;
    }
    auto AddII = findInstructionIterator(*AddBB, AddInst);
    if (AddII == AddBB->end()) {
      warnJTInfoMismatch(BC, JT, Info, "add instruction was not found", BF,
                         MetadataAddInst, Info.AddAddress);
      return false;
    }

    if (AddInst->getOpcode() != AArch64::ADDXrx &&
        AddInst->getOpcode() != AArch64::ADDXrs) {
      warnJTInfoMismatch(BC, JT, Info,
                         "add instruction is not a supported jump-table add",
                         BF, AddInst, Info.AddAddress);
      return false;
    }

    const MCPhysReg EntryReg = AddInst->getOperand(2).getReg();
    BinaryBasicBlock *LoadBB = nullptr;
    MCInst *LoadInst =
        findInstructionAtAddress(*BF, Info.LoadAddress, &LoadBB);
    if (!LoadInst || !LoadBB ||
        findInstructionIterator(*LoadBB, LoadInst) == LoadBB->end()) {
      warnJTInfoMismatch(BC, JT, Info, "load instruction was not found", BF,
                         findInstructionAtAddress(*BF, Info.LoadAddress),
                         Info.LoadAddress);
      return false;
    }
    if (!BC.MIB->mayLoad(*LoadInst) ||
        MCPlus::getNumPrimeOperands(*LoadInst) == 0 ||
        !LoadInst->getOperand(0).isReg() || !AddInst->getOperand(2).isReg()) {
      warnJTInfoMismatch(BC, JT, Info,
                         "load instruction is not a supported jump-table load",
                         BF, LoadInst, Info.LoadAddress);
      return false;
    }

    auto getReg64Alias = [&](MCPhysReg Reg) -> MCPhysReg {
      if (isReg64(*BC.MRI, Reg))
        return Reg;
      return static_cast<MCPhysReg>(BC.MRI->getMatchingSuperReg(
          Reg, AArch64::sub_32,
          &BC.MRI->getRegClass(AArch64::GPR64RegClassID)));
    };

    const MCPhysReg LoadReg64 = getReg64Alias(LoadInst->getOperand(0).getReg());
    const MCPhysReg EntryReg64 = getReg64Alias(EntryReg);
    if (LoadReg64 == AArch64::NoRegister || EntryReg64 == AArch64::NoRegister ||
        LoadReg64 != EntryReg64) {
      warnJTInfoMismatch(BC, JT, Info,
                         "load instruction is not a supported jump-table load",
                         BF, LoadInst, Info.LoadAddress);
      return false;
    }

    BinaryBasicBlock *AdrBB = nullptr;
    MCInst *AdrInst = findInstructionAtAddress(*BF, Info.AdrAddress, &AdrBB);
    if (!AdrInst || !AdrBB) {
      warnJTInfoMismatch(BC, JT, Info, "adr instruction was not found", BF,
                         findInstructionAtAddress(*BF, Info.AdrAddress),
                         Info.AdrAddress);
      return false;
    }
    auto AdrII = findInstructionIterator(*AdrBB, AdrInst);
    if (AdrII == AdrBB->end()) {
      warnJTInfoMismatch(BC, JT, Info, "adr instruction was not found", BF,
                         findInstructionAtAddress(*BF, Info.AdrAddress),
                         Info.AdrAddress);
      return false;
    }

    const MCPhysReg BaseReg = AddInst->getOperand(1).getReg();
    MCInst *AdrAddInst = nullptr;
    if (BC.MIB->isADR(*AdrInst)) {
      if (MCPlus::getNumPrimeOperands(*AdrInst) == 0 ||
          !AdrInst->getOperand(0).isReg() ||
          AdrInst->getOperand(0).getReg() != BaseReg) {
        warnJTInfoMismatch(BC, JT, Info,
                           "base materialization is not a supported "
                           "jump-table base",
                           BF, AdrInst, Info.AdrAddress);
        return false;
      }
    } else if (BC.MIB->isADRP(*AdrInst)) {
      auto AdrAddII = std::next(AdrII);
      while (AdrAddII != AdrBB->end() && BC.MIB->isPseudo(*AdrAddII))
        ++AdrAddII;
      if (AdrAddII == AdrBB->end()) {
        warnJTInfoMismatch(BC, JT, Info,
                           "relaxed ADRP+ADD sequence was not found", BF,
                           AdrInst, Info.AdrAddress);
        return false;
      }
      AdrAddInst = &*AdrAddII;
      if (!BC.MIB->isAddXri(*AdrAddInst) ||
          !BC.MIB->matchAdrpAddPair(*AdrInst, *AdrAddInst) ||
          MCPlus::getNumPrimeOperands(*AdrAddInst) < 3 ||
          !AdrAddInst->getOperand(0).isReg() ||
          !AdrAddInst->getOperand(1).isReg() ||
          AdrAddInst->getOperand(0).getReg() != BaseReg ||
          AdrAddInst->getOperand(1).getReg() != BaseReg) {
        warnJTInfoMismatch(BC, JT, Info,
                           "relaxed ADRP+ADD sequence is not a supported "
                           "jump-table base",
                           BF, AdrAddInst, Info.AdrAddress);
        return false;
      }
    } else {
      warnJTInfoMismatch(BC, JT, Info,
                         "base materialization is not a supported jump-table "
                         "base",
                         BF, AdrInst, Info.AdrAddress);
      return false;
    }

    SmallVector<std::pair<MCInst *, uint64_t>, 2> ReferenceInsts;
    ReferenceInsts.reserve(Info.References.size());
    for (uint64_t RefAddress : Info.References) {
      BinaryFunction *RefBF = BC.getBinaryFunctionContainingAddress(RefAddress);
      if (!RefBF || RefBF != BF ||
          RefBF->getState() != BinaryFunction::State::CFG)
        continue;

      MCInst *RefInst = findInstructionAtAddress(*RefBF, RefAddress);
      if (!RefInst)
        continue;
      ReferenceInsts.push_back({RefInst, RefAddress});
    }

    Sites.emplace_back(AArch64JTSiteInfo{
        Info, *BF, *BranchBB, *AdrInst, AdrAddInst, *LoadInst, *AddInst,
        *BranchInst,
        std::move(ReferenceInsts)});
  }

  return !Sites.empty();
}

static bool canRewriteSite(const BinaryContext &BC,
                           const JumpTable &JT,
                           const AArch64JTSiteInfo &Site,
                           JumpTable::JumpTableType NewType,
                           bool NeedsBaseRewrite) {
  assert(!NeedsBaseRewrite ||
         BC.MIB->isADR(Site.AdrInst) ||
         (BC.MIB->isADRP(Site.AdrInst) && Site.AdrAddInst &&
          BC.MIB->matchAdrpAddPair(Site.AdrInst, *Site.AdrAddInst)));

  if (MCPlus::getNumPrimeOperands(Site.LoadInst) < 5 ||
      MCPlus::getNumPrimeOperands(Site.AddInst) < 4)
    return false;

  if (!matchesAArch64JTLoadOpcode(BC, Site, JT.Type))
    return false;

  if (!Site.LoadInst.getOperand(0).isReg() ||
      !Site.LoadInst.getOperand(2).isReg())
    return false;

  const MCRegisterInfo &MRI = *BC.MRI;
  const bool IndexIs64 = isReg64(MRI, Site.LoadInst.getOperand(2).getReg());
  if (!getAArch64JTLoadOpcode(NewType, IndexIs64))
    return false;

  if (NeedsBaseRewrite && BC.MIB->isADRP(Site.AdrInst) &&
      (!Site.AdrAddInst ||
       !BC.MIB->matchAdrpAddPair(Site.AdrInst, *Site.AdrAddInst)))
    return false;

  switch (NewType) {
  case JumpTable::JTT_AARCH64_U8_X4:
  case JumpTable::JTT_AARCH64_U16_X4:
  case JumpTable::JTT_AARCH64_U32_X4:
  case JumpTable::JTT_AARCH64_I32:
  case JumpTable::JTT_NORMAL:
    return true;
  default:
    return false;
  }
}

static bool rewriteSite(BinaryContext &BC, const JumpTable &JT,
                        AArch64JTSiteInfo &Site,
                        JumpTable::JumpTableType NewType,
                        const MCSymbol *NewBaseSymbol, bool NeedsBaseRewrite) {
  MCInst &Load = Site.LoadInst;
  MCInst &Add = Site.AddInst;

  const MCRegisterInfo &MRI = *BC.MRI;
  const MCPhysReg OldLoadReg = Load.getOperand(0).getReg();
  const MCPhysReg Reg32 = isReg64(MRI, OldLoadReg)
                              ? static_cast<MCPhysReg>(
                                    MRI.getSubReg(OldLoadReg, AArch64::sub_32))
                              : OldLoadReg;
  const MCPhysReg Reg64 =
      isReg64(MRI, OldLoadReg)
          ? OldLoadReg
          : static_cast<MCPhysReg>(MRI.getMatchingSuperReg(
                OldLoadReg, AArch64::sub_32,
                &MRI.getRegClass(AArch64::GPR64RegClassID)));
  assert(Reg32 != AArch64::NoRegister &&
         "expected 64-bit AArch64 GPR to have a 32-bit alias");
  assert(Reg64 != AArch64::NoRegister &&
         "expected 32-bit AArch64 GPR to have a 64-bit alias");
  assert(matchesAArch64JTLoadOpcode(BC, Site, JT.Type) &&
         "expected jump table load opcode to match the current table format");

  const bool IndexIs64 = isReg64(MRI, Load.getOperand(2).getReg());
  std::optional<unsigned> NewLoadOpc =
      getAArch64JTLoadOpcode(NewType, IndexIs64);
  if (!NewLoadOpc) {
    warnJTInfoMismatch(BC, JT, Site.JTInfo,
                       Twine("promotion to ") +
                           JumpTable::jumpTableTypeName(NewType) +
                           " failed: no supported load opcode matches the "
                           "load shape",
                       &Site.BF, &Load, Site.JTInfo.LoadAddress);
    return false;
  }

  Load.setOpcode(*NewLoadOpc);
  switch (NewType) {
  case JumpTable::JTT_AARCH64_U8_X4:
  case JumpTable::JTT_AARCH64_U16_X4:
  case JumpTable::JTT_AARCH64_U32_X4:
    Load.getOperand(0).setReg(Reg32);
    break;
  case JumpTable::JTT_AARCH64_I32:
  case JumpTable::JTT_NORMAL:
    Load.getOperand(0).setReg(Reg64);
    break;
  default:
    return false;
  }

  // Keep index extend mode and offset register untouched, only update scaling.
  Load.getOperand(4).setImm(NewType == JumpTable::JTT_AARCH64_U8_X4 ? 0 : 1);

  switch (NewType) {
  case JumpTable::JTT_AARCH64_U8_X4:
  case JumpTable::JTT_AARCH64_U16_X4:
  case JumpTable::JTT_AARCH64_U32_X4:
    Add.setOpcode(AArch64::ADDXrx);
    Add.getOperand(2).setReg(Reg32);
    Add.getOperand(3).setImm(
        AArch64_AM::getArithExtendImm(AArch64_AM::UXTW, 2));
    break;
  case JumpTable::JTT_AARCH64_I32:
    Add.setOpcode(AArch64::ADDXrs);
    Add.getOperand(2).setReg(Reg64);
    Add.getOperand(3).setImm(0);
    break;
  case JumpTable::JTT_NORMAL:
    Add.setOpcode(AArch64::ADDXrs);
    Add.getOperand(1).setReg(Reg64);
    Add.getOperand(2).setReg(AArch64::XZR);
    Add.getOperand(3).setImm(0);
    break;
  default:
    return false;
  }

  if (NeedsBaseRewrite) {
    assert(NewBaseSymbol && "base symbol required for base rewrite");
    if (BC.MIB->isADR(Site.AdrInst)) {
      if (!BC.MIB->replaceMemOperandDisp(Site.AdrInst, NewBaseSymbol,
                                         BC.Ctx.get())) {
        warnJTInfoMismatch(
            BC, JT, Site.JTInfo,
            Twine("promotion to ") + JumpTable::jumpTableTypeName(NewType) +
                " failed: could not retarget the ADR base",
            &Site.BF, &Site.AdrInst, Site.JTInfo.AdrAddress);
        return false;
      }
    } else {
      assert(BC.MIB->isADRP(Site.AdrInst) && Site.AdrAddInst &&
             BC.MIB->matchAdrpAddPair(Site.AdrInst, *Site.AdrAddInst) &&
             "expected jump table metadata base site to be ADR or relaxed "
             "ADRP+ADD");
      const int64_t Addend = BC.MIB->getTargetAddend(*Site.AdrAddInst);
      if (!BC.MIB->setOperandToSymbolRef(Site.AdrInst, /*OpNum=*/1,
                                         NewBaseSymbol, Addend, BC.Ctx.get(),
                                         ELF::R_AARCH64_NONE) ||
          !BC.MIB->setOperandToSymbolRef(*Site.AdrAddInst, /*OpNum=*/2,
                                         NewBaseSymbol, Addend, BC.Ctx.get(),
                                         ELF::R_AARCH64_ADD_ABS_LO12_NC)) {
        warnJTInfoMismatch(
            BC, JT, Site.JTInfo,
            Twine("promotion to ") + JumpTable::jumpTableTypeName(NewType) +
                " failed: could not retarget the relaxed ADRP+ADD base",
            &Site.BF, &Site.AdrInst, Site.JTInfo.AdrAddress);
        return false;
      }
    }
  }

  for (const std::pair<MCInst *, uint64_t> &Ref : Site.ReferenceInsts) {
    if (!rewriteJTReference(BC, JT, *Ref.first)) {
      warnJTInfoMismatch(
          BC, JT, Site.JTInfo,
          Twine("promotion to ") + JumpTable::jumpTableTypeName(NewType) +
              " failed: could not update a jump-table-relative reference",
          &Site.BF, Ref.first, Ref.second);
      return false;
    }
  }

  return true;
}

static SmallVector<JumpTable::JumpTableType, 4>
getTypeLadder(JumpTable::JumpTableType OldType) {
  switch (OldType) {
  case JumpTable::JTT_AARCH64_I8_X4:
  case JumpTable::JTT_AARCH64_I16_X4:
    return {JumpTable::JTT_AARCH64_U16_X4, JumpTable::JTT_AARCH64_U32_X4,
            JumpTable::JTT_AARCH64_I32, JumpTable::JTT_NORMAL};
  case JumpTable::JTT_AARCH64_U8_X4:
    return {JumpTable::JTT_AARCH64_U8_X4, JumpTable::JTT_AARCH64_U16_X4,
            JumpTable::JTT_AARCH64_U32_X4, JumpTable::JTT_AARCH64_I32,
            JumpTable::JTT_NORMAL};
  case JumpTable::JTT_AARCH64_U16_X4:
    return {JumpTable::JTT_AARCH64_U16_X4, JumpTable::JTT_AARCH64_U32_X4,
            JumpTable::JTT_AARCH64_I32, JumpTable::JTT_NORMAL};
  case JumpTable::JTT_AARCH64_U32_X4:
    return {JumpTable::JTT_AARCH64_U32_X4, JumpTable::JTT_AARCH64_I32,
            JumpTable::JTT_NORMAL};
  case JumpTable::JTT_AARCH64_I32:
    return {JumpTable::JTT_AARCH64_U32_X4, JumpTable::JTT_AARCH64_I32,
            JumpTable::JTT_NORMAL};
  case JumpTable::JTT_NORMAL:
    return {JumpTable::JTT_NORMAL};
  default:
    return {};
  }
}

static bool isUnsignedX4Type(JumpTable::JumpTableType Type) {
  return Type == JumpTable::JTT_AARCH64_U8_X4 ||
         Type == JumpTable::JTT_AARCH64_U16_X4 ||
         Type == JumpTable::JTT_AARCH64_U32_X4;
}

static bool isSignedX4Type(JumpTable::JumpTableType Type) {
  return Type == JumpTable::JTT_AARCH64_I8_X4 ||
         Type == JumpTable::JTT_AARCH64_I16_X4;
}

static Error failToLegalizeJumpTable(BinaryContext &BC, const JumpTable &JT,
                                     const BinaryFunction &BF,
                                     const Twine &Reason) {
  BC.errs() << "BOLT-ERROR: unable to legalize AArch64 jump table "
            << JT.getFirstLabel()->getName() << " in " << BF << ": " << Reason
            << '\n';
  return createFatalBOLTError("");
}

} // namespace

Error AArch64JumpTablePromotion::runOnFunctions(BinaryContext &BC) {
  if (!BC.isAArch64() || !BC.HasRelocations || BC.JumpTableInfos.empty())
    return Error::success();

  uint64_t NumChanged = 0;
  SmallPtrSet<JumpTable *, 8> ProcessedJTs;

  for (BinaryFunction *BF : BC.getAllBinaryFunctions()) {
    if (!shouldOptimize(*BF))
      continue;

    for (const std::pair<const uint64_t, JumpTable *> &JTI :
         BF->jumpTables()) {
      JumpTable *JT = JTI.second;
      if (!ProcessedJTs.insert(JT).second
          || JT->Entries.empty()
          || !isSupportedAArch64JTType(JT->Type)
          || !JumpTable::isAArch64Type(JT->Type))
        continue;

      BinaryFunction *AnchorBF = BF;

      // Populate output offsets/ranges for basic blocks based on the current
      // instruction stream and layout, so jump table encoding decisions can be
      // made using post-optimization distances.
      BC.calculateEmittedSize(*AnchorBF, /*FixBranches=*/false);

      SmallVector<uint64_t, 16> TargetOffsets;
      TargetOffsets.reserve(JT->Entries.size());
      bool ResolutionFailed = false;
      uint64_t MinTargetOffset = std::numeric_limits<uint64_t>::max();
      const MCSymbol *MinTargetSymbol = nullptr;
      for (const MCSymbol *Entry : JT->Entries) {
        std::optional<uint64_t> Offset =
            resolveSymbolOutputOffset(*AnchorBF, *Entry);
        if (!Offset) {
          ResolutionFailed = true;
          break;
        }
        TargetOffsets.push_back(*Offset);
        if (*Offset < MinTargetOffset) {
          MinTargetOffset = *Offset;
          MinTargetSymbol = Entry;
        }
      }
      if (ResolutionFailed || !MinTargetSymbol) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            "failed to resolve post-layout offsets for jump table targets");
      }

      if (!JT->AArch64BaseSymbol) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            "missing base symbol required for AArch64 jump table encoding");
      }

      std::optional<uint64_t> OldBaseOffset =
          resolveSymbolOutputOffset(*AnchorBF, *JT->AArch64BaseSymbol);
      if (!OldBaseOffset) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            "failed to resolve post-layout offset for jump table base");
      }

      SmallVector<int64_t, 16> CurrentDeltas;
      CurrentDeltas.reserve(TargetOffsets.size());
      for (uint64_t TargetOffset : TargetOffsets)
        CurrentDeltas.push_back((int64_t)(TargetOffset - *OldBaseOffset));

      // If the current encoding still fits after layout, no legalization is
      // required. Only rewrite when the final layout made the original
      // encoding invalid.
      const bool CurrentTypeUsable =
          JT->Type == JumpTable::JTT_NORMAL ||
          canEncodeAArch64RelativeInOutput(*AnchorBF, *JT->AArch64BaseSymbol,
                                           JT->Entries);
      if (fitsAArch64JTType(JT->Type, CurrentDeltas) && CurrentTypeUsable)
        continue;

      SmallVector<AArch64JTSiteInfo, 4> Sites;
      if (!collectJTSiteInfo(BC, *JT, Sites)) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("current encoding ") +
                JumpTable::jumpTableTypeName(JT->Type) +
                " no longer fits final layout and the jump-table branch "
                "sequence could not be rewritten");
      }

      for (const AArch64JTSiteInfo &Site : Sites) {
        if (matchesAArch64JTLoadOpcode(BC, Site, JT->Type))
          continue;
        warnJTInfoMismatch(
            BC, *JT, Site.JTInfo, "load opcode does not match table format",
            &Site.BF, &Site.LoadInst, Site.JTInfo.LoadAddress);
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("load opcode does not match table format ") +
                JumpTable::jumpTableTypeName(JT->Type));
      }

      SmallVector<JumpTable::JumpTableType, 4> Ladder = getTypeLadder(JT->Type);
      if (Ladder.empty()) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("no legal promotion path available from ") +
                JumpTable::jumpTableTypeName(JT->Type));
      }

      JumpTable::JumpTableType SelectedType = JT->Type;
      const MCSymbol *SelectedBaseSymbol = JT->AArch64BaseSymbol;
      bool SelectedNeedsBaseRewrite = false;
      bool FoundChoice = false;
      for (JumpTable::JumpTableType CandidateType : Ladder) {
        bool NeedsBaseRewrite = false;
        uint64_t CandidateBase = *OldBaseOffset;
        const MCSymbol *CandidateBaseSymbol = JT->AArch64BaseSymbol;

        if (CandidateType == JumpTable::JTT_NORMAL) {
          CandidateBaseSymbol = nullptr;
        } else if (isUnsignedX4Type(CandidateType) &&
                   isSignedX4Type(JT->Type)) {
          CandidateBase = MinTargetOffset;
          CandidateBaseSymbol = MinTargetSymbol;
          NeedsBaseRewrite = CandidateBaseSymbol != JT->AArch64BaseSymbol;
        }

        SmallVector<int64_t, 16> Deltas;
        Deltas.reserve(TargetOffsets.size());
        for (uint64_t TargetOffset : TargetOffsets)
          Deltas.push_back((int64_t)(TargetOffset - CandidateBase));

        const bool Fits = fitsAArch64JTType(CandidateType, Deltas);
        if (!Fits)
          continue;

        if (CandidateType != JumpTable::JTT_NORMAL &&
            (!CandidateBaseSymbol ||
             !canEncodeAArch64RelativeInOutput(*AnchorBF,
                                               *CandidateBaseSymbol,
                                               JT->Entries)))
          continue;

        if (!llvm::all_of(Sites, [&](const AArch64JTSiteInfo &Site) {
              return canRewriteSite(BC, *JT, Site, CandidateType,
                                    NeedsBaseRewrite);
            }))
          continue;

        SelectedType = CandidateType;
        SelectedBaseSymbol = CandidateBaseSymbol;
        SelectedNeedsBaseRewrite = NeedsBaseRewrite;
        FoundChoice = true;
        break;
      }

      if (!FoundChoice) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("current encoding ") +
                JumpTable::jumpTableTypeName(JT->Type) +
                " no longer fits final layout and no supported wider encoding "
                "could be applied");
      }

      const bool TypeChanged = SelectedType != JT->Type;
      const bool BaseChanged = SelectedBaseSymbol != JT->AArch64BaseSymbol;
      if (!TypeChanged && !BaseChanged) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("current encoding ") +
                JumpTable::jumpTableTypeName(JT->Type) +
                " no longer fits final layout");
      }

      bool RewriteSuccess = true;
      for (AArch64JTSiteInfo &Site : Sites) {
        if (!rewriteSite(BC, *JT, Site, SelectedType, SelectedBaseSymbol,
                         SelectedNeedsBaseRewrite)) {
          RewriteSuccess = false;
          break;
        }
      }
      if (!RewriteSuccess) {
        return failToLegalizeJumpTable(
            BC, *JT, *AnchorBF,
            Twine("failed to rewrite branch sequence for promotion to ") +
                JumpTable::jumpTableTypeName(SelectedType));
      }

      JT->Type = SelectedType;
      JT->EntrySize = BC.getJumpTableEntrySize(SelectedType);
      JT->OutputEntrySize = JT->EntrySize;
      JT->AArch64BaseSymbol = const_cast<MCSymbol *>(SelectedBaseSymbol);

      if (BinaryData *BD = BC.getBinaryDataAtAddress(JT->getAddress()))
        BD->updateSize(JT->getSize());

      if (opts::Verbosity >= 1) {
        BC.outs() << "BOLT-INFO: updated AArch64 jump table "
                  << JT->getFirstLabel()->getName() << " to "
                  << JumpTable::jumpTableTypeName(SelectedType) << '\n';
      }
      ++NumChanged;
    }
  }

  if (opts::Verbosity >= 1 && NumChanged)
    BC.outs() << "BOLT-INFO: promoted " << NumChanged
              << " AArch64 jump table(s)\n";

  return Error::success();
}

} // namespace bolt
} // namespace llvm
