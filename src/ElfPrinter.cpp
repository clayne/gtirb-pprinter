//===- ElfPrinter.cpp -------------------------------------------*- C++ -*-===//
//
//  Copyright (C) 2019 GrammaTech, Inc.
//
//  This code is licensed under the MIT license. See the LICENSE file in the
//  project root for license terms.
//
//  This project is sponsored by the Office of Naval Research, One Liberty
//  Center, 875 N. Randolph Street, Arlington, VA 22203 under contract #
//  N68335-17-C-0700.  The content of the information does not necessarily
//  reflect the position or policy of the Government and no official
//  endorsement should be inferred.
//
//===----------------------------------------------------------------------===//
#include "ElfPrinter.h"

#include <elf.h>

namespace gtirb_pprint {
///
/// Print a comment that automatically scopes.
///
class BlockAreaComment {
public:
  BlockAreaComment(std::ostream& ss, std::string m = std::string{},
                   std::function<void()> f = []() {})
      : ofs{ss}, message{std::move(m)}, func{std::move(f)} {
    ofs << '\n';

    if (!message.empty()) {
      ofs << "# BEGIN - " << this->message << '\n';
    }

    func();
  }

  ~BlockAreaComment() {
    func();

    if (!message.empty()) {
      ofs << "# END   - " << this->message << '\n';
    }

    ofs << '\n';
  }

  std::ostream& ofs;
  const std::string message;
  std::function<void()> func;
};

ElfSyntax::ElfSyntax() : Syntax() {}

const std::string& ElfSyntax::Comment() const { return commentStyle; }

const std::string& ElfSyntax::Byte() const { return byteDirective; }
const std::string& ElfSyntax::Long() const { return longDirective; }
const std::string& ElfSyntax::Quad() const { return quadDirective; }
const std::string& ElfSyntax::Word() const { return wordDirective; }

const std::string& ElfSyntax::Text() const { return textDirective; }
const std::string& ElfSyntax::Data() const { return dataDirective; }
const std::string& ElfSyntax::Bss() const { return bssDirective; }

const std::string& ElfSyntax::Section() const { return sectionDirective; }
const std::string& ElfSyntax::Global() const { return globalDirective; }
const std::string& ElfSyntax::Align() const { return alignDirective; }
const std::string& ElfSyntax::Type() const { return typeDirective; }

ElfPrettyPrinter::ElfPrettyPrinter(gtirb::Context& context_, gtirb::IR& ir_,
                                   const ElfSyntax& syntax_,
                                   const PrintingPolicy& policy_)
    : PrettyPrinterBase(context_, ir_, syntax_, policy_), elfSyntax(syntax_) {
  if (this->ir.modules()
          .begin()
          ->getAuxData<
              std::map<gtirb::Offset,
                       std::vector<std::tuple<std::string, std::vector<int64_t>,
                                              gtirb::UUID>>>>(
              "cfiDirectives")) {
    policy.skipSections.insert(".eh_frame");
  }
}

const PrintingPolicy& ElfPrettyPrinter::DefaultPrintingPolicy() {
  static PrintingPolicy defaultPolicy{
      /// Sections to avoid printing.
      {".comment", ".plt", ".init", ".fini", ".got", ".plt.got", ".got.plt",
       ".plt.sec", ".eh_frame_hdr"},

      /// Functions to avoid printing.
      {"_start", "deregister_tm_clones", "register_tm_clones",
       "__do_global_dtors_aux", "frame_dummy", "__libc_csu_fini",
       "__libc_csu_init", "_dl_relocate_static_pie"},

      /// Sections with possible data object exclusion.
      {".init_array", ".fini_array"},
  };
  return defaultPolicy;
}

void ElfPrettyPrinter::printSectionHeaderDirective(
    std::ostream& os, const gtirb::Section& section) {
  const std::string& sectionName = section.getName();
  os << syntax.Section() << ' ' << sectionName;
}

void ElfPrettyPrinter::printSectionProperties(std::ostream& os,
                                              const gtirb::Section& section) {
  const auto* elfSectionProperties =
      this->ir.modules()
          .begin()
          ->getAuxData<std::map<gtirb::UUID, std::tuple<uint64_t, uint64_t>>>(
              "elfSectionProperties");
  if (!elfSectionProperties)
    return;
  const auto sectionProperties = elfSectionProperties->find(section.getUUID());
  if (sectionProperties == elfSectionProperties->end())
    return;
  uint64_t type = std::get<0>(sectionProperties->second);
  uint64_t flags = std::get<1>(sectionProperties->second);
  os << " ,\"";
  if (flags & SHF_WRITE)
    os << "w";
  if (flags & SHF_ALLOC)
    os << "a";
  if (flags & SHF_EXECINSTR)
    os << "x";
  os << "\"";
  if (type == SHT_PROGBITS)
    os << ",@progbits";
  if (type == SHT_NOBITS)
    os << ",@nobits";
}

void ElfPrettyPrinter::printSectionFooterDirective(
    std::ostream& /* os */, const gtirb::Section& /* section */) {}

void ElfPrettyPrinter::printFunctionHeader(std::ostream& os, gtirb::Addr addr) {
  const std::string& name = this->getFunctionName(addr);

  if (!name.empty()) {
    const BlockAreaComment bac(os, "Function Header",
                               [this, &os]() { printBar(os, false); });
    printAlignment(os, addr);
    os << syntax.Global() << ' ' << name << '\n';
    os << elfSyntax.Type() << ' ' << name << ", @function\n";
    os << name << ":\n";
  }
}

void ElfPrettyPrinter::printFunctionFooter(std::ostream& /* os */,
                                           gtirb::Addr /* addr */) {}

void ElfPrettyPrinter::printByte(std::ostream& os, std::byte byte) {
  auto flags = os.flags();
  os << syntax.Byte() << " 0x" << std::hex << static_cast<uint32_t>(byte)
     << '\n';
  os.flags(flags);
}

void ElfPrettyPrinter::printFooter(std::ostream& /* os */){};

bool ElfPrettyPrinter::shouldExcludeDataElement(
    const gtirb::Section& section, const gtirb::DataObject& dataObject) const {
  if (!policy.arraySections.count(section.getName()))
    return false;
  const gtirb::Module& module = *this->ir.modules().begin();
  auto foundSymbolic = module.findSymbolicExpression(dataObject.getAddress());
  if (foundSymbolic != module.symbolic_expr_end()) {
    if (const auto* s = std::get_if<gtirb::SymAddrConst>(&*foundSymbolic)) {
      return skipEA(*s->Sym->getAddress());
    }
  }
  return false;
}

} // namespace gtirb_pprint