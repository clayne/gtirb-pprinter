#include "Logger.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fstream>
#include <gtirb_layout/gtirb_layout.hpp>
#include <gtirb_pprinter/ElfBinaryPrinter.hpp>
#include <gtirb_pprinter/PrettyPrinter.hpp>
#include <gtirb_pprinter/version.h>
#include <iomanip>
#include <iostream>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

static fs::path getAsmFileName(const fs::path& InitialPath, int Index) {
  if (Index == 0)
    return InitialPath;

  // Add the number to the end of the stem of the filename.
  std::string Filename = InitialPath.stem().generic_string();
  Filename.append(std::to_string(Index));
  Filename.append(InitialPath.extension().generic_string());
  fs::path FinalPath = InitialPath.parent_path();
  FinalPath /= Filename;
  return FinalPath;
}

int main(int argc, char** argv) {
  gtirb_pprint::registerAuxDataTypes();

  po::options_description desc("Allowed options");
  desc.add_options()("help,h", "Produce help message.");
  desc.add_options()("version,V", "Print version info and exit.");
  desc.add_options()("ir,i", po::value<std::string>(), "GTIRB file to print.");
  desc.add_options()(
      "asm,a", po::value<std::string>(),
      "The name of the assembly output file. If none is given, gtirb-pprinter "
      "prints to the standard output. If the IR has more "
      "than one module, files of the form FILE, FILE_2 ... "
      "FILE_n with the content of each of the modules");
  desc.add_options()("binary,b", po::value<std::string>(),
                     "The name of the binary output file.");
  desc.add_options()("compiler-args,c",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Additional arguments to pass to the compiler. Only used "
                     "for binary printing.");
  desc.add_options()("library-paths,L",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Library paths to be passed to the linker. Only used "
                     "for binary printing.");
  desc.add_options()("module,m", po::value<int>()->default_value(0),
                     "The index of the module to be printed if printing to the "
                     "standard output.");
  desc.add_options()("format,f", po::value<std::string>(),
                     "The format of the target binary object.");
  desc.add_options()("syntax,s", po::value<std::string>(),
                     "The syntax of the assembly file to generate.");
  desc.add_options()("layout,l", "Layout code and data in memory to "
                                 "avoid overlap");
  desc.add_options()("debug,d", "Turn on debugging (will break assembly)");

  desc.add_options()("keep-function",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Print the given function even if they are skipped"
                     " by default (e.g. _start).");
  desc.add_options()("skip-function",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Do not print the given function.");
  desc.add_options()("keep-all-functions",
                     "Do not use the default list of functions to skip.");

  desc.add_options()("keep-symbol",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Print the given symbol even if they are skipped"
                     " by default (e.g. __TMC_END__).");
  desc.add_options()("skip-symbol",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Do not print the given symbol.");
  desc.add_options()("keep-all-symbols",
                     "Do not use the default list of symbols to skip.");

  desc.add_options()("keep-section",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Print the given section even if they are skipped by "
                     "default (e.g. .text).");
  desc.add_options()("skip-section",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Do not print the given section.");
  desc.add_options()("keep-all-sections",
                     "Do not use the default list of sections to skip.");

  desc.add_options()(
      "keep-array-section", po::value<std::vector<std::string>>()->multitoken(),
      "Print the given array section even if they are skipped by "
      "default (e.g. .fini_array).");
  desc.add_options()("skip-array-section",
                     po::value<std::vector<std::string>>()->multitoken(),
                     "Do not print the contents of the given array section.");
  desc.add_options()("keep-all-array-sections",
                     "Do not use the default list of array sections to skip.");

  desc.add_options()("keep-all,k", "Combination of --keep-all-functions, "
                                   "--keep-all-symbols, --keep-all-sections, "
                                   "and --keep-all-array-sections.");

  po::positional_options_description pd;
  pd.add("ir", -1);
  po::variables_map vm;
  try {
    po::store(
        po::command_line_parser(argc, argv).options(desc).positional(pd).run(),
        vm);
    if (vm.count("help") != 0) {
      std::cout << desc << "\n";
      return 1;
    }
    if (vm.count("version") != 0) {
      std::cout << GTIRB_PPRINTER_VERSION_STRING << " ("
                << GTIRB_PPRINTER_BUILD_REVISION << " "
                << GTIRB_PPRINTER_BUILD_DATE << ")\n";
      return 0;
    }
  } catch (std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\nTry '" << argv[0]
              << " --help' for more information.\n";
    return 1;
  }
  po::notify(vm);

  gtirb::Context ctx;
  gtirb::IR* ir;

  if (vm.count("ir") != 0) {
    fs::path irPath = vm["ir"].as<std::string>();
    if (fs::exists(irPath)) {
      LOG_INFO << std::setw(24) << std::left << "Reading IR: " << irPath
               << std::endl;
      std::ifstream in(irPath.string(), std::ios::in | std::ios::binary);
      ir = gtirb::IR::load(ctx, in);
    } else {
      LOG_ERROR << "IR not found: \"" << irPath << "\".";
      return EXIT_FAILURE;
    }
  } else {
    ir = gtirb::IR::load(ctx, std::cin);
  }
  if (ir->modules().empty()) {
    LOG_ERROR << "IR has no modules";
    return EXIT_FAILURE;
  }

  // Layout IR in memory without overlap.
  if (vm.count("layout") || gtirb_layout::layoutRequired(*ir)) {
    for (auto& M : ir->modules()) {
      LOG_INFO << "Applying new layout to module " << M.getUUID() << "..."
               << std::endl;
      gtirb_layout::layoutModule(ctx, M);
    }
  } else {
    for (auto& M : ir->modules()) {
      if (std::any_of(M.symbols_begin(), M.symbols_end(),
                      [](const gtirb::Symbol& Sym) {
                        return !Sym.hasReferent() && Sym.getAddress();
                      })) {
        LOG_INFO << "Module " << M.getUUID()
                 << " has integral symbols; attempting to assign referents..."
                 << std::endl;
        gtirb_layout::fixIntegralSymbols(ctx, M);
      }
    }
  }

  // Perform the Pretty Printing step.
  gtirb_pprint::PrettyPrinter pp;
  pp.setDebug(vm.count("debug"));
  const std::string& format =
      vm.count("format")
          ? vm["format"].as<std::string>()
          : gtirb_pprint::getModuleFileFormat(*ir->modules().begin());
  const std::string& syntax =
      vm.count("syntax") ? vm["syntax"].as<std::string>()
                         : gtirb_pprint::getDefaultSyntax(format).value_or("");
  auto target = std::make_tuple(format, syntax);
  if (gtirb_pprint::getRegisteredTargets().count(target) == 0) {
    LOG_ERROR << "Unsupported combination: format '" << format
              << "' and syntax '" << syntax << "'\n";
    std::string::size_type width = 0;
    for (const auto& [f, s] : gtirb_pprint::getRegisteredTargets())
      width = std::max({width, f.size(), s.size()});
    width += 2; // add "gutter" between columns
    LOG_ERROR << "Available combinations:\n";
    LOG_ERROR << "    " << std::setw(width) << "format"
              << "syntax\n";
    for (const auto& [f, s] : gtirb_pprint::getRegisteredTargets())
      LOG_ERROR << "    " << std::setw(width) << f << s << '\n';
    return EXIT_FAILURE;
  }
  pp.setTarget(std::move(target));

  if (vm.count("keep-all") != 0) {
    pp.functionPolicy().useDefaults(false);
    pp.symbolPolicy().useDefaults(false);
    pp.sectionPolicy().useDefaults(false);
    pp.arraySectionPolicy().useDefaults(false);
  }

  if (vm.count("keep-all-functions") != 0) {
    pp.functionPolicy().useDefaults(false);
  }
  if (vm.count("keep-function") != 0) {
    for (const auto& S : vm["keep-function"].as<std::vector<std::string>>()) {
      pp.functionPolicy().keep(S);
    }
  }
  if (vm.count("skip-function") != 0) {
    for (const auto& S : vm["skip-function"].as<std::vector<std::string>>()) {
      pp.functionPolicy().skip(S);
    }
  }

  if (vm.count("keep-all-symbols") != 0) {
    pp.symbolPolicy().useDefaults(false);
  }
  if (vm.count("keep-symbol") != 0) {
    for (const auto& S : vm["keep-symbol"].as<std::vector<std::string>>()) {
      pp.symbolPolicy().keep(S);
    }
  }
  if (vm.count("skip-symbol") != 0) {
    for (const auto& S : vm["skip-symbol"].as<std::vector<std::string>>()) {
      pp.symbolPolicy().skip(S);
    }
  }

  if (vm.count("keep-all-sections") != 0) {
    pp.sectionPolicy().useDefaults(false);
  }
  if (vm.count("keep-section") != 0) {
    for (const auto& S : vm["keep-section"].as<std::vector<std::string>>()) {
      pp.sectionPolicy().keep(S);
    }
  }
  if (vm.count("skip-section") != 0) {
    for (const auto& S : vm["skip-section"].as<std::vector<std::string>>()) {
      pp.sectionPolicy().skip(S);
    }
  }

  if (vm.count("keep-all-array-sections") != 0) {
    pp.sectionPolicy().useDefaults(false);
  }
  if (vm.count("keep-array-section") != 0) {
    for (const auto& S :
         vm["keep-array-section"].as<std::vector<std::string>>()) {
      pp.arraySectionPolicy().keep(S);
    }
  }
  if (vm.count("skip-array-section") != 0) {
    for (const auto& S :
         vm["skip-array-section"].as<std::vector<std::string>>()) {
      pp.arraySectionPolicy().skip(S);
    }
  }

  // Write ASM to a file.
  if (vm.count("asm") != 0) {
    const auto asmPath = fs::path(vm["asm"].as<std::string>());
    if (!asmPath.has_filename()) {
      LOG_ERROR << "The given path " << asmPath << " has no filename"
                << std::endl;
      return EXIT_FAILURE;
    }
    int i = 0;
    for (gtirb::Module& m : ir->modules()) {
      fs::path name = getAsmFileName(asmPath, i);
      std::ofstream ofs(name.generic_string());
      if (ofs) {
        pp.print(ofs, ctx, m);
        LOG_INFO << "Module " << i << "'s assembly written to: " << name
                 << "\n";
      } else {
        LOG_ERROR << "Could not output assembly output file: " << asmPath
                  << "\n";
      }
      ++i;
    }
  }
  // Link directly to a binary.
  if (vm.count("binary") != 0) {
    gtirb_bprint::ElfBinaryPrinter binaryPrinter(true);
    const auto binaryPath = fs::path(vm["binary"].as<std::string>());
    std::vector<std::string> extraCompilerArgs;
    if (vm.count("compiler-args") != 0)
      extraCompilerArgs = vm["compiler-args"].as<std::vector<std::string>>();
    std::vector<std::string> libraryPaths;
    if (vm.count("library-paths") != 0)
      libraryPaths = vm["library-paths"].as<std::vector<std::string>>();
    binaryPrinter.link(binaryPath.string(), extraCompilerArgs, libraryPaths, pp,
                       ctx, *ir);
  }
  // Write ASM to the standard output if no other action was taken.
  if ((vm.count("asm") == 0) && (vm.count("binary") == 0)) {
    gtirb::Module* module = nullptr;
    int i = 0;
    for (gtirb::Module& m : ir->modules()) {
      if (i == vm["module"].as<int>()) {
        module = &m;
        break;
      }
      ++i;
    }
    if (!module) {
      LOG_ERROR << "The ir has " << i << " modules, module with index "
                << vm["module"].as<int>() << " cannot be printed" << std::endl;
      return EXIT_FAILURE;
    }
    pp.print(std::cout, ctx, *module);
  }

  return EXIT_SUCCESS;
}
