//===- PeBinaryPrinter.cpp --------------------------------------*- C++ -*-===//
//
//  Copyright (C) 2020 GrammaTech, Inc.
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
#include "PeBinaryPrinter.hpp"
#include "file_utils.hpp"
#include <iostream>

namespace gtirb_bprint {
void PeBinaryPrinter::prepareAssemblerArguments(
    const std::vector<std::string>& compilands, gtirb::IR& ir,
    const std::string& outputFilename,
    const std::vector<std::string>& extraCompilerArgs,
    const std::vector<std::string>& libraryPaths,
    std::vector<std::string>& args) const {
  // FIXME: various improvements left to be made:
  // * gtirb-pprinter doesn't currently support x86, so support for the ml
  // assembler is incomplete.
  // * GTIRB does not yet provide access to the PE32 header, so there's no way
  // to determine whether the module was an executable or a DLL, what subsystem
  // the module was compiled for, what the stack size is, etc. We are currently
  // treating everything as an executable unless it has no entrypoint, and are
  // using symbols in the module to guess whether it's a console application
  // or not.
  // * The user can specify command line arguments, but there's no way to
  // distinguish between options to ml64.exe per compiland or options to
  // link.exe for the whole executable.

  // The command lines for ml and ml64 are different, but there are some common
  // features. The first thing are the options common to both ml and ml64,
  // followed by assembler-specific options, followed by the name of the file
  // to be assembled. For ml64 compilations, the linker arguments follow all of
  // the compliands.
  bool isML64 = compiler == "ml64";

  // Disable the banner for the assembler.
  args.push_back("/nologo");

  // Set one-time options like the output file name.
  args.push_back("/Fe");
  args.push_back(outputFilename);

  // Set per-compiland options, if any.
  for (const std::string& compiland : compilands) {
    // Copy in any user-supplied command line arguments.
    std::copy(extraCompilerArgs.begin(), extraCompilerArgs.end(),
              std::back_inserter(args));

    // The last thing before the next file is the file to be assembled.
    args.push_back(compiland);
  }

  // Handle the linker options for ml64.
  if (isML64) {
    // Start the linker arguments.
    args.push_back("/link");

    // Disable the banner for the linker.
    args.push_back("/nologo");

    // If the user specified additional library paths, tell the linker about
    // them now. Note, there is no way to do this for ml, as it does not accept
    // linker command line arguments.
    for (const std::string& libPath : libraryPaths)
      args.push_back("/LIBPATH:" + libPath);

    // If there's an entrypoint defined in any module, specify it on the
    // command line. This works around the fact that ml64 cannot automatically
    // determine what the entrypoint is.
    if (auto Iter = std::find_if(ir.modules_begin(), ir.modules_end(),
                                 [](const gtirb::Module& M) {
                                   return M.getEntryPoint() != nullptr;
                                 });
        Iter != ir.modules_end()) {
      // By convention, we name the entrypoint this way. See the constructor
      // for MasmPrettyPrinter for more information.
      args.push_back("/entry:__EntryPoint");

      // If we found a module with an entrypoint, see if that module also
      // contains a symbol that gives us a hint as to whether it's a console
      // application or not. If there is a symbol named main or wmain, then
      // set the subsystem to console, otherwise assume it to be Windows (as
      // opposed to a kernel driver, etc).
      bool isConsole = !Iter->findSymbols("main").empty() ||
                       !Iter->findSymbols("wmain").empty();

      if (isConsole)
        args.push_back("/subsystem:console");
      else
        args.push_back("/subsystem:windows");
    } else {
      // We could not find an entrypoint, so assume this is a resource-only DLL
      // with no entry point as a fallback.
      args.push_back("/DLL");
      args.push_back("/NOENTRY");
    }
  }
}

PeBinaryPrinter::PeBinaryPrinter() : compiler("ml64") {}

int PeBinaryPrinter::link(const std::string& outputFilename,
                          const std::vector<std::string>& extraCompilerArgs,
                          const std::vector<std::string>& userLibraryPaths,
                          const gtirb_pprint::PrettyPrinter& pp,
                          gtirb::Context& ctx, gtirb::IR& ir) const {
  // Prepare all of the files we're going to generate assembly into.
  std::vector<TempFile> tempFiles;
  std::vector<std::string> tempFileNames;
  if (!prepareSources(ctx, ir, pp, tempFiles, tempFileNames)) {
    std::cerr << "ERROR: Could not write assembly into a temporary file.\n";
    return -1;
  }

  // Collect the arguments for invoking the assembler.
  std::vector<std::string> args;
  prepareAssemblerArguments(tempFileNames, ir, outputFilename,
                            extraCompilerArgs, userLibraryPaths, args);

  // Invoke the assembler.
  bool toolFound;
  if (!execute(compiler, args, &toolFound)) {
    if (!toolFound)
      std::cerr << "ERROR: could not find the assembler '" << compiler
                << "' on the PATH.\n";
    else
      std::cerr << "ERROR: assembler returned a nonzero exit code.\n";
    return -1;
  }
  return 0;
}

} // namespace gtirb_bprint
