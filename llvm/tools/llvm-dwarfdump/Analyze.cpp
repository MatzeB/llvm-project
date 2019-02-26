// facebook begin D13311561
//===-- Analyze.cpp -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm-dwarfdump.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"

#include <vector>
#include <map>
#include <set>

using namespace llvm;
using namespace dwarf;
using namespace object;

extern cl::opt<uint32_t> MaxDirectoryDepth;
extern cl::opt<uint32_t> MinTypeDuplicationSize;
extern cl::opt<uint32_t> MaxInlineDepth;
extern cl::opt<float> MinPercentage;
extern cl::opt<bool> Verbose;

static bool isDwarfSectionName(StringRef Name) {
  return Name.startswith(".debug") || Name.startswith("__debug") ||
         Name.startswith(".apple") || Name.startswith("__apple");
}

static DWARFDie GetParentDeclContextDIE(DWARFDie &Die) {
  if (DWARFDie SpecDie =
          Die.getAttributeValueAsReferencedDie(dwarf::DW_AT_specification)) {
    if (DWARFDie SpecParent = GetParentDeclContextDIE(SpecDie))
      return SpecParent;
  }
  if (DWARFDie AbstDie =
          Die.getAttributeValueAsReferencedDie(dwarf::DW_AT_abstract_origin)) {
    if (DWARFDie AbstParent = GetParentDeclContextDIE(AbstDie))
      return AbstParent;
  }

  // We never want to follow parent for inlined subroutine - that would
  // give us information about where the function is inlined, not what
  // function is inlined
  if (Die.getTag() == dwarf::DW_TAG_inlined_subroutine)
    return DWARFDie();

  DWARFDie ParentDie = Die.getParent();
  if (!ParentDie)
    return DWARFDie();

  switch (ParentDie.getTag()) {
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_subprogram:
    return ParentDie; // Found parent decl context DIE
  case dwarf::DW_TAG_lexical_block:
    return GetParentDeclContextDIE(ParentDie);
  default:
    break;
  }

  return DWARFDie();
}

std::string getQualifiedName(DWARFDie Die) {
  // If the dwarf has mangled name, use mangled name
  if (const char *LinkageName = Die.getName(DINameKind::LinkageName))
    return std::string(LinkageName);

  StringRef ShortName(Die.getName(DINameKind::ShortName));
  if (ShortName.empty())
    return "";

  const uint32_t Language = dwarf::toUnsigned(
      Die.getDwarfUnit()->getUnitDIE().find(dwarf::DW_AT_language), 0);
  // For C++ and ObjC++, prepend names of all parent declaration contexts
  if (!(Language == dwarf::DW_LANG_C_plus_plus ||
        Language == dwarf::DW_LANG_C_plus_plus_03 ||
        Language == dwarf::DW_LANG_C_plus_plus_11 ||
        Language == dwarf::DW_LANG_C_plus_plus_14 ||
        Language == dwarf::DW_LANG_ObjC_plus_plus ||
        // This should not be needed for C, but we see C++ code marked as C
        // in some binaries. This should hurt, so let's do it for C as well
        Language == dwarf::DW_LANG_C))
    return ShortName.str();

  std::string Name = ShortName.str();
  for (DWARFDie ParentDie = GetParentDeclContextDIE(Die); ParentDie;
       ParentDie = GetParentDeclContextDIE(ParentDie)) {
    StringRef ParentName(ParentDie.getName(DINameKind::ShortName));
    if (!ParentName.empty())
        Name = ParentName.str() + "::" + Name;
  }
  return Name;
}

typedef std::vector<DWARFDie> DIECollection;

void GetTopLevelInlineDies(DWARFDie Die, DIECollection &InlineDies) {
  // Given an a DIE, find all top level inline funciton DIEs and return then
  // in InlineDies.
  for (const auto &ChildDie: Die.children()) {
    switch (ChildDie.getTag()) {
    case DW_TAG_inlined_subroutine:
      InlineDies.push_back(ChildDie);
      break;
    case DW_TAG_lexical_block:
      // Look into any number of lexical blocks to find all top level
      // inlined functions
      GetTopLevelInlineDies(ChildDie, InlineDies);
      break;
    default:
      break;
    }
  }
}

uint64_t CalculateDieRangeSize(DWARFDie Die) {
  // Calculate the byte size of the address ranges for a DW_TAG_subprogram or
  // DW_TAG_inlined_subroutine Die by figuring out the size of the ranges in
  // bytes and subtracting any child DW_TAG_inlined_subroutine ranges from
  // the total.
  uint64_t DieRangeSize = 0;
  if (auto RangesOrError = Die.getAddressRanges()) {
    for (const auto &Range: RangesOrError.get()) {
      if (Range.LowPC < Range.HighPC)
        DieRangeSize += Range.HighPC - Range.LowPC;
    }
  }
  return DieRangeSize;
}

uint64_t CalculateTopLevelInlineDieRangeSizes(DWARFDie Die) {

  // Find all top level inline dies so we can subtract off their ranges
  DIECollection InlineDies;
  uint64_t InlineDieRangeSize = 0;
  GetTopLevelInlineDies(Die, InlineDies);
  for (const auto &InlineDie: InlineDies) {
    if (auto RangesOrError = InlineDie.getAddressRanges()) {
      for (const auto &Range: RangesOrError.get()) {
        if (Range.LowPC < Range.HighPC)
          InlineDieRangeSize += Range.HighPC - Range.LowPC;
      }
    }
  }
  return InlineDieRangeSize;
}

namespace {
// A helper class to print out percentages with 2 digits past the decimal with
// optional width.
struct Percent {
  uint64_t Value; ///< Value to use when displaying percentage
  uint64_t Total; ///< Total count to use when calculating percentage
  uint32_t Width; ///< Display width in characters
  float percentage() const {
    if (Value == 0 || Total == 0)
      return 0.0f;
    return ((float)Value/(float)Total) * 100.0f;
  }
  Percent(uint64_t V, uint64_t T, uint32_t W): Value(V), Total(T), Width(W) {}
};
// A helper class to dump human readable file sizes (988B, 1.23K, 2.34G)
struct ReadableFileSize {
  uint64_t Size; ///< Byte size to display as human readable value
  uint64_t Total; ///< Display percentage if non zero
  uint32_t Width; ///< Display width in characters
  ReadableFileSize(uint64_t S, uint64_t T, uint32_t W) : 
      Size(S), Total(T), Width(W) {};
};
// A helper class to display float values with 2 digits past the decimal with
// optional widths for tabular display
struct Float {
  float Value; ///< Float value to display
  uint32_t Width; ///< Total width in bytes to use when printing float
  Float(float V, uint32_t W) : Value(V), Width(W) {}
};

raw_ostream &operator<<(raw_ostream &OS, const Float &F) {
  char FormatStr[32];
  const uint32_t Width = F.Width < 2 ? 2 : F.Width;
  snprintf(FormatStr, sizeof(FormatStr), "%%%u.2f", Width);
  OS << format(FormatStr, F.Value);
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const Percent &PV) {
  const uint32_t FloatWidth = PV.Width ? PV.Width-1 : 0;
  OS << Float{PV.percentage(), FloatWidth} << '%';
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const ReadableFileSize &RFS) {
  if (RFS.Size < 1024)
    OS << format_decimal(RFS.Size, RFS.Width);
  else {
    float Size = (float)RFS.Size;
    int i = 0;
    const char* units[] = {"", "K", "M", "G", "T"};
    while (Size > 1024.0) {
      Size /= 1024.0;
      i++;
    }
    OS << Float{Size, RFS.Width ? RFS.Width-1 : 0} << units[i];
  }
  if (RFS.Total > 0)
    OS << ' ' << Percent(RFS.Size, RFS.Total, RFS.Width);
  return OS;
}

// A single entity in a declaration context for a DIE. This is used in
// DIEDeclContext as a container used in maps when tracking types.
class DWARFDeclContext {
  dwarf::Tag Tag;
  StringRef Name;
public:
  DWARFDeclContext(): Tag(DW_TAG_null), Name() {}
  DWARFDeclContext(const DWARFDeclContext &RHS) = default;
  DWARFDeclContext(const DWARFDie &Die) :
    Tag(Die.getTag()),
    Name(Die.getName(DINameKind::ShortName)) {
  }

  dwarf::Tag getTag() const { return Tag; }
  StringRef getName() const { return Name; }

  void dump(raw_ostream &OS) const {
    OS << dwarf::TagString(Tag) << "(\"" << getName() << "\")\n";
  }

  bool operator==(const DWARFDeclContext &RHS) const {
    return Tag == RHS.Tag && Name == RHS.Name;
  }

  bool operator<(const DWARFDeclContext &RHS) const {
    if (Tag == RHS.Tag)
      return Name < RHS.Name;
    return Tag < RHS.Tag;
  }
};

// A collection of DWARFDeclContext items that uniquely identify a DIE that
// conforms to the 1 definition rule.
class DIEDeclContext {

  std::vector<DWARFDeclContext> Context;

  static void GetDeclContext(const DWARFDie &Die,
                             std::vector<DWARFDeclContext> &Ctx) {
    if (!Die)
      return;
    GetDeclContext(GetDeclContextParent(Die), Ctx);
    Ctx.push_back(Die);
  }

  static DWARFDie
  GetDeclContextParent(const DWARFDie &OrigDie) {
    if (OrigDie) {
      DWARFDie Die = OrigDie;

      while (Die) {
        if (OrigDie != Die) {
          switch (Die.getTag()) {
          case DW_TAG_lexical_block:
            return GetDeclContextParent(Die);
          case DW_TAG_namespace:
          case DW_TAG_structure_type:
          case DW_TAG_union_type:
          case DW_TAG_class_type:
          case DW_TAG_subprogram:
            return Die;
          case DW_TAG_inlined_subroutine: {
            DWARFDie AbsDie =
                Die.getAttributeValueAsReferencedDie(DW_AT_abstract_origin);
            if (AbsDie)
              return AbsDie;
            break;
          }
          default:
            break;
          }
        }

        DWARFDie SpecDie =
            Die.getAttributeValueAsReferencedDie(DW_AT_specification);
        if (SpecDie) {
          DWARFDie DeclCtxDie = GetDeclContextParent(SpecDie);
          if (DeclCtxDie)
            return DeclCtxDie;
        }

        DWARFDie AbsDie =
            Die.getAttributeValueAsReferencedDie(DW_AT_abstract_origin);
        if (AbsDie) {
          DWARFDie DeclCtxDie = GetDeclContextParent(AbsDie);
          if (DeclCtxDie)
            return DeclCtxDie;
        }

        Die = Die.getParent();
      }
    }
    return DWARFDie();
  }

public:
  DIEDeclContext(const DWARFDie &Die) {
    GetDeclContext(Die, Context);
  }
  void dump(raw_ostream &OS) const {
    bool First = true;
    for (const auto &Ctx: Context) {
      if (First)
        First = false;
      else
        OS << "::";
      OS << Ctx.getName();
    }
  }
  bool empty() const {
    return Context.empty();
  }
  DWARFDeclContext pop_front() {
    DWARFDeclContext DDC;
    if (!Context.empty()) {
      DDC = Context.front();
      Context.erase(Context.begin());
    }
    return DDC;
  }
  void dump_verbose(raw_ostream &OS) const {
    for (const auto &Ctx: Context)
      Ctx.dump(OS);
    OS << "\n";
  }
  DWARFDeclContext operator[](size_t Index) const {
    if (Index < Context.size())
      return Context[Index];
    return DWARFDeclContext();
  }
  bool operator==(const DIEDeclContext &RHS) const {
    return Context == RHS.Context;
  }
  bool operator<(const DIEDeclContext &RHS) const {
    return Context < RHS.Context;
  }
};

// Type DIEs are considered equal if they are defined on the same file and line
// and have the same byte size and their DIEDeclContext objects match.
class DIETypeInfo {
  std::string File;
  size_t Size;
  uint32_t Line;
public:
  DIETypeInfo(const DWARFDie &Die) {
    if (unsigned FileNum =
            dwarf::toUnsigned(Die.find(dwarf::DW_AT_decl_file), 0))
      File = Die.getFile(FileNum);
    Size = dwarf::toUnsigned(Die.find(dwarf::DW_AT_byte_size), 0);
    Line = dwarf::toUnsigned(Die.find(dwarf::DW_AT_decl_line), 0);
  }
  void dump(raw_ostream &OS, bool ShowByteSize) const {
    OS << File << ":" << Line;
    if (ShowByteSize)
      OS << " <" << Size << ">";
  }
  bool operator==(const DIETypeInfo &RHS) const {
    return Size == RHS.Size && Line == RHS.Line && File == RHS.File;
  }
  bool operator<(const DIETypeInfo &RHS) const {
    if (Size != RHS.Size)
      return Size < RHS.Size;
    if (Line != RHS.Line)
      return Line < RHS.Line;
    return File < RHS.File;
  }
};

struct InlineData {
  uint64_t Count = 0;
  uint64_t CodeSize = 0;
};

class InlineNode {
  uint64_t Count = 0;
  uint64_t CodeSize = 0;
  typedef std::map<DWARFDeclContext, InlineNode> MapType;
  MapType Children;

  bool getNextDeclContext(DIEDeclContext &DDC, DWARFDeclContext &DC) {
    while (!DDC.empty()) {
      DC = DDC.pop_front();
      switch (DC.getTag()) {
        case DW_TAG_lexical_block:
        case DW_TAG_subprogram:
          break;
        default:
          return true;
      }
    }
    return false;
  }

  void _insert(DIEDeclContext &DDC, uint64_t Size, uint64_t InlineSize) {

    DWARFDeclContext DC;
    if (!getNextDeclContext(DDC, DC))
      return;
    auto Pos = Children.find(DC);
    if (Pos != Children.end()) {
      Pos->second.Count++;
      Pos->second.CodeSize += Size - InlineSize;
      Pos->second._insert(DDC, Size, InlineSize);
    } else {
      Children.emplace(std::make_pair(std::move(DC), 
                                      InlineNode(DDC, Size, InlineSize)));
    }

  }
  InlineNode(DIEDeclContext &DDC, uint64_t Size, uint64_t InlineSize) : 
      Count(1), CodeSize(Size - InlineSize) {
    DWARFDeclContext DC;
    if (!getNextDeclContext(DDC, DC))
      return;
    Children.emplace(std::make_pair(DC, InlineNode(DDC, Size, InlineSize)));
  }
public:
  InlineNode() = default;
  void insert(DWARFDie Die) {
    DIEDeclContext DDC(Die);
    if (DDC.empty())
      return;
    const uint64_t Size = CalculateDieRangeSize(Die);
    const uint64_t InlineSize = CalculateTopLevelInlineDieRangeSizes(Die);
    _insert(DDC, Size, InlineSize);
  }

  void dump(raw_ostream &OS, uint64_t TotalCodeSize, size_t Depth) const {
    std::multimap<uint64_t, MapType::const_iterator, std::greater<uint64_t> > 
        SizeToIter;
    if (Depth >= MaxInlineDepth)
      return;
    uint64_t OtherInlineCount = 0;
    uint64_t OtherInlineCodeSize = 0;
    for (auto Pos = Children.begin(), End = Children.end(); Pos != End; ++Pos) {
      if (!Verbose) {
        Percent InlineCodePercent(Pos->second.CodeSize, TotalCodeSize, 0);
        if (InlineCodePercent.percentage() < MinPercentage) {
          OtherInlineCount += Pos->second.Count;
          OtherInlineCodeSize += Pos->second.CodeSize;
          continue;
        }
      }
      SizeToIter.insert(std::make_pair(Pos->second.CodeSize, Pos));
    }
    for (const auto &Pair: SizeToIter) {
      OS << format_decimal(Pair.second->second.Count, 7) << ' '
         << ReadableFileSize(Pair.second->second.CodeSize, TotalCodeSize, 7);
      OS.indent(Depth*2+1);
      OS << Pair.second->first.getName() << '\n';
      Pair.second->second.dump(OS, TotalCodeSize, Depth+1);
    }
    // if (OtherInlineCodeSize > 0) {
    //   OS << format_decimal(OtherInlineCount, 7) << ' ' 
    //      << ReadableFileSize(OtherInlineCodeSize, TotalCodeSize, 7);
    //   OS.indent(Depth*2+1);
    //   OS << "Other inlined functions ( -min-percent=" 
    //      << Float(MinPercentage, 0) << " )\n";
    // }
  }
};

typedef std::map<DIEDeclContext, DIECollection> DDCToDiesMap;
typedef std::set<uint64_t> DIEOfssetSet;
typedef std::set<DWARFDie> DIESet;
typedef std::map<uint64_t, DIECollection> OffsetToDiesMap;
typedef StringMap<size_t> PathToSizeMap;

struct DWARFStatistics {
  DDCToDiesMap DDCToDies;
  DIESet TypeDies;
  OffsetToDiesMap RefOffsetToReferencingDies;
  PathToSizeMap CUPathToSize;
  InlineNode Inline;
  DWARFAddressRangesVector TextRanges;
  DIECollection SubprogramsNotInText;
  uint64_t TotalCodeSize = 0;
  uint64_t TotalInlinedCodeSize = 0;
  bool containsTextAddress(uint64_t Addr) const {
    if (TextRanges.empty())
      return true; // Some synthetic test case files have no valid text ranges.
    return llvm::find_if(TextRanges, [Addr](const DWARFAddressRange &r) {
         return r.LowPC <= Addr && Addr < r.HighPC;
       }) != TextRanges.end();
  }
};

class DirInfo {
  StringRef Component;
  uint64_t Size = 0;
  typedef std::map<StringRef, DirInfo> NameToDirInfo;
  NameToDirInfo Children;

  void _insert(sys::path::const_iterator Pos, sys::path::const_iterator End,
               uint64_t S) {
    Size += S;
    if (Pos != End) {
      StringRef component(*Pos);
      auto CPos = Children.find(component);
      if (CPos == Children.end()) {
        Children.insert(std::make_pair(component, DirInfo(component)));
        CPos = Children.find(component);
      }
      CPos->second._insert(++Pos, End, S);
    }
  }

  void _dump(raw_ostream &OS, uint64_t TotalSize, uint32_t Depth,
             uint32_t DisplayDepth) const {
    if (DisplayDepth >= MaxDirectoryDepth)
      return;
    uint32_t ChildDepth = DisplayDepth + 1;
    if (Depth == 0)
      --ChildDepth;
    else {
      OS << ReadableFileSize(Size, TotalSize, 7);
      OS.indent(DisplayDepth*2+1) << Component << '\n';
    }
    std::map<uint64_t, NameToDirInfo::const_iterator,
             std::greater<uint64_t> > SizeToIter;
    for (auto Pos = Children.begin(), End = Children.end(); Pos != End; ++Pos)
      SizeToIter[Pos->second.size()] = Pos;
    for (const auto &Pair: SizeToIter)
      Pair.second->second._dump(OS, TotalSize, Depth+1, ChildDepth);
  }

public:
  DirInfo(StringRef component) : Component(component) {}

  void insert(StringRef Path, uint64_t Size) {
    auto Pos = sys::path::begin(Path);
    auto End = sys::path::end(Path);
    while (Pos != End) {
      StringRef component(*Pos);
      if (component.empty() || component == "/")
        ++Pos;
      else
        break;
    }
    _insert(Pos, End, Size);
  }

  uint64_t size() const { return Size; }
  void dump(raw_ostream &OS, uint64_t TotalSize) const {
    uint32_t DisplayDepth = 0;
    uint32_t Depth = 0;
    _dump(OS, TotalSize, Depth, DisplayDepth);
  }
};

} // anonymous namespace

static size_t getDIEDWARFSize(const DWARFDie &Die) {
  auto DieOffset = Die.getOffset();
  auto SiblingOffset = Die.getSibling().getOffset();
  return SiblingOffset - DieOffset;
}

static void analyzeDie(const DWARFDie &Die, DWARFStatistics &Stats) {
  const auto Tag = Die.getTag();
  switch (Tag) {
    case dwarf::DW_TAG_typedef:
      Stats.TypeDies.insert(Die);
      break;

    case dwarf::DW_TAG_structure_type:
    case dwarf::DW_TAG_class_type:
    case dwarf::DW_TAG_union_type:
    case dwarf::DW_TAG_enumeration_type:
      // Keep a set of type DIE offsets so we can verify they are referenced.
      Stats.TypeDies.insert(Die);
      // Skip forward declarations
      if (dwarf::toUnsigned(Die.find(dwarf::DW_AT_declaration), 0) != 0)
        break;
      // Only add the type to the map if it has a name and if it has children.
      // If a type doesn't have children, it is often a forward declaration.
      if (Die.getName(DINameKind::ShortName) && Die.hasChildren())
        Stats.DDCToDies[DIEDeclContext(Die)].push_back(Die);
      break;
    case dwarf::DW_TAG_subprogram:
      if (auto RangesOrError = Die.getAddressRanges()) {
        for (const auto &Range: RangesOrError.get()) {
          uint64_t CodeSize = 0;
          if (Range.LowPC < Range.HighPC) {
            if (!Stats.containsTextAddress(Range.LowPC))
              Stats.SubprogramsNotInText.push_back(Die);
            CodeSize += Range.HighPC - Range.LowPC;
          }
          Stats.TotalCodeSize += CodeSize;
          Stats.TotalInlinedCodeSize +=
              CalculateTopLevelInlineDieRangeSizes(Die);
        }
      }
      break;
    case dwarf::DW_TAG_inlined_subroutine:
      Stats.Inline.insert(Die);
      break;
    default:
      break;
  }
  for (const auto &Attr: Die.attributes()) {
    if (auto Reference = Attr.Value.getAsReference())
      Stats.RefOffsetToReferencingDies[*Reference].push_back(Die);
  }
  for (const auto &ChildDie: Die.children())
    analyzeDie(ChildDie, Stats);
}

bool dwarfdump::analyzeObjectFile(ObjectFile &Obj, DWARFContext &DICtx,
                       const Twine Filename, raw_ostream &OS) {
  DWARFStatistics Stats;
  const size_t FileSize = Obj.getData().size();

  OS << "File: \"" << Filename << "\"\n" << "Size: " << FileSize;
  if (FileSize > 1024)
    OS << " ("  << ReadableFileSize(FileSize, 0, 0) << ")";
  OS << '\n';

  // Calculate the total size of all DWARF sections
  uint64_t TotalDwarfSize = 0;
  std::map<uint64_t, SectionRef, std::greater<uint64_t> > SizeToSection;
  StringRef Name;
  for (const auto &Sect: Obj.sections()) {
    if (Expected<StringRef> NameOrErr = Sect.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());
    const auto SectSize = Sect.getSize();
    if (isDwarfSectionName(Name)) {
      TotalDwarfSize += SectSize;
      SizeToSection[SectSize] = Sect;
    }
    // Keep track of any sections with code so we can warn when functions are
    // not contained in an section that is marked as executable.
    if (SectSize > 0 && Sect.isText()) {
      const auto SectAddr = Sect.getAddress();
      Stats.TextRanges.push_back(DWARFAddressRange(SectAddr,
                                                   SectAddr + SectSize));
    }
  }

  // Report DWARF section sizes relative to total DWARF size and to file size
  OS << "DWARF section sizes:\n";
  OS << "SIZE    % DWARF % FILE  SECTION NAME\n";
  OS << "------- ------- ------- -------------------------------------\n";
  for (const auto &Pair: SizeToSection) {
    SectionRef Sect = Pair.second;
    if (Expected<StringRef> NameOrErr = Sect.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());
    const auto SectSize = Sect.getSize();
    if (isDwarfSectionName(Name))
      OS << ReadableFileSize(SectSize, TotalDwarfSize, 7) << ' '
         << Percent(SectSize, FileSize, 7) << ' ' << Name << '\n';
  }
  OS << "======= ======= ======= =====================================\n";
  OS << ReadableFileSize(TotalDwarfSize, TotalDwarfSize, 7) << ' '
     << Percent(TotalDwarfSize, FileSize, 7) << " Total DWARF Size\n\n";

  // Iterate through all compile units and all DIEs so we can analyze them
  for (const auto &CU : DICtx.compile_units()) {
    auto CUDie = CU->getUnitDIE(false);
    // Some compile units have headers and no valid CU DIE
    if (!CUDie)
      continue;
    // Analyze the DIEs and collect info in Stats
    analyzeDie(CUDie, Stats);
    // Get the path name to the source file and keep track of the size of
    // .debug_info for each source file. This allows us to report debug info
    // size per directory below.
    Name = CUDie.getName(DINameKind::ShortName);
    if (Name.empty())
      continue;
    if (sys::path::is_relative(Name)) {
      const char *CompDir = CU->getCompilationDir();
      if (CompDir) {
        llvm::SmallString<64> ResolvedPath;
        ResolvedPath.append(CompDir);
        auto PathStyle = sys::path::Style::native;
        sys::path::append(ResolvedPath, PathStyle, Name);
        sys::path::remove_dots(ResolvedPath, true, PathStyle);
        Stats.CUPathToSize[ResolvedPath.str().str()] = CU->getLength();
        continue;
      }
    }
    Stats.CUPathToSize[Name] = CU->getLength();
  }

  size_t TotalDupSize = 0;
  std::multimap<uint64_t, std::string, std::greater<uint64_t> > SizeToDesc;
  uint64_t OtherDupTypeSize = 0;
  uint64_t OtherDupTypeCount = 0;
  for (const auto &Pair: Stats.DDCToDies) {
    if (Pair.second.size() == 1)
      continue;
    std::map<DIETypeInfo, DIECollection> DIETypeInfoToDies;
    for (const auto &Die: Pair.second) {
      DIETypeInfoToDies[DIETypeInfo(Die)].push_back(Die);
    }
    for (const auto &type_info_and_dies: DIETypeInfoToDies) {
      // Report info about all types whose file and line and size match
      size_t NumDups = type_info_and_dies.second.size();
      if (NumDups <= 1)
        continue;
      size_t DupSize = 0; // Size of all DIEs in the DWARF
      for (const auto &Die: type_info_and_dies.second)
        DupSize += getDIEDWARFSize(Die);
      // Remove the debug info for the first DIE from the total
      DupSize -= getDIEDWARFSize(type_info_and_dies.second.front());
      TotalDupSize += DupSize;
      if (DupSize <= MinTypeDuplicationSize || 
          Percent(DupSize, TotalDwarfSize, 0).percentage() < MinPercentage) {
        OtherDupTypeSize += DupSize;
        OtherDupTypeCount += NumDups - 1;
        continue;
      }
      std::string Description;
      raw_string_ostream DescOS(Description);
      DescOS << ReadableFileSize(DupSize, TotalDwarfSize, 7) << ' '
              << Percent(DupSize, FileSize, 7) << ' '
              << format_decimal(NumDups-1, 6) << ' ';
      Pair.first.dump(DescOS);
      DescOS << " @ ";
      type_info_and_dies.first.dump(DescOS, false);
      DescOS << "\n";
      DescOS.flush();
      SizeToDesc.insert(std::make_pair(DupSize,
                                        std::move(Description)));
    }
  }

  PathToSizeMap DirsToSize;
  for (const auto &Pair: Stats.CUPathToSize) {
    auto FullPath = Pair.getKey();
    auto Path = FullPath.substr(0, FullPath.rfind('/'));
    DirsToSize[Path] += Pair.getValue();
  }
  DirInfo RootDirInfo("");
  for (auto Pos = DirsToSize.begin(), End = DirsToSize.end(); Pos != End; ++Pos)
    RootDirInfo.insert(Pos->getKey(), Pos->getValue());

  OS << "\nDWARF .debug_info size in bytes by source directory:\n";
  OS << "SIZE    % DWARF DIRECTORY\n";
  OS << "------- ------- -------------------------------------\n";
  RootDirInfo.dump(OS, TotalDwarfSize);

  // Dump out the sorted type duplication information
  auto reportOther = [&]() {
    // Report all types that fell under the -min-type-size and -min-percent
      OS << ReadableFileSize(OtherDupTypeSize, TotalDwarfSize, 7)
         << ' ' << Percent(OtherDupTypeSize, FileSize, 7) << ' ' 
         << format_decimal(OtherDupTypeCount, 6) 
         << " Other duplicated types ( ";
      if (MinTypeDuplicationSize)
        OS << "-min-type-size=" << MinTypeDuplicationSize << ' ';
      if (MinPercentage != 0.0)
        OS << "-min-percent=" << Float(MinPercentage, 0) << ' ';
      OS << ")\n";
  };
  OS << "\nType duplication:\n";
  OS << "SIZE    % DWARF % FILE  DUPS   DESCRIPTION\n";
  OS << "------- ------- ------- ------ ------------------------------------\n";
  for (const auto &Pair: SizeToDesc) {
    // Report all types that fell under the -min-type-size and -min-percent
    if (OtherDupTypeSize > Pair.first ) {
      reportOther();
      OtherDupTypeSize = 0;
    }
    OS << Pair.second;
  }
  if (OtherDupTypeSize > 0)
      reportOther();
  // Report the total duplicated byte size
  OS << "======= ======= ======= ====== ====================================\n"
     << ReadableFileSize(TotalDupSize, TotalDwarfSize, 7)
     << ' ' << Percent(TotalDupSize, FileSize, 7)
     << "        Total duplicated types\n";
  // Report info on types that were not referenced
  uint64_t UnreferencedTypeDieByteSize = 0;
  auto RefMapEnd = Stats.RefOffsetToReferencingDies.end();
  for (const auto Die: Stats.TypeDies) {
    auto Pos = Stats.RefOffsetToReferencingDies.find(Die.getOffset());
    if (Pos != RefMapEnd)
      continue;
    bool ChildDieWasReferenced = false;
    for (auto ChildDie: Die.children()) {
      Pos = Stats.RefOffsetToReferencingDies.find(ChildDie.getOffset());
      if (Pos != RefMapEnd) {
        ChildDieWasReferenced = true;
        break;
      }
    }
    if (!ChildDieWasReferenced)
      UnreferencedTypeDieByteSize += getDIEDWARFSize(Die);
  }
  if (UnreferencedTypeDieByteSize > 0) {
    OS << ReadableFileSize(UnreferencedTypeDieByteSize, TotalDwarfSize, 7)
       << ' ' << Percent(UnreferencedTypeDieByteSize, FileSize, 7)
       << "        Bytes used for types that were not referenced.\n";
  }


  OS << "\nInline information:\n";
  OS << "  Code size: " << Stats.TotalCodeSize << '\n';
  OS << "  Inline code size: " << Stats.TotalInlinedCodeSize << '\n';
  OS << "  Inline code percentage: " 
     << Percent(Stats.TotalInlinedCodeSize, Stats.TotalCodeSize, 0) << '\n';
  OS << "COUNT   SIZE    % CODE  Name\n";
  OS << "------- ------- ------- -------------------------------------------\n";
  Stats.Inline.dump(OS, Stats.TotalCodeSize, 0);

  return true;
}
