/*
 * Copyright (C) 2016  Steffen Nüssle
 * rf - refactor
 *
 * This file is part of rf.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <memory>
#include <cstdlib>

#ifdef __unix__
#include <unistd.h>
#endif

#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/Refactoring.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/Support/CommandLine.h>

#include <Refactorers/EnumConstantRefactorer.hpp>
#include <Refactorers/FunctionRefactorer.hpp>
#include <Refactorers/IncludeRefactorer.hpp>
#include <Refactorers/MacroRefactorer.hpp>
#include <Refactorers/NamespaceRefactorer.hpp>
#include <Refactorers/TagRefactorer.hpp>
#include <Refactorers/VariableRefactorer.hpp>

#include <util/memory.hpp>
#include <util/CommandLine.hpp>

#include <RefactoringActionFactory.hpp>

static llvm::cl::OptionCategory RefactoringOptions("Code refactoring options");
static llvm::cl::OptionCategory FlagOptions("Flags");

/* clang-format off */
static llvm::cl::extrahelp HelpText(
    "\n!! Commit your source code to a version control system before "
    "refactoring it !!\n\n"
);

static llvm::cl::list<std::string> EnumConstantVec(
    "enum-constant",
    llvm::cl::desc(
        "Refactor an enumeration constant."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)
);

static llvm::cl::list<std::string> FunctionVec(
    "function",
    llvm::cl::desc(
        "Refactor a function or class method name."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)
);

static llvm::cl::list<std::string> NamespaceVec(
    "namespace",
    llvm::cl::desc(
        "Refactor a namespace."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)
);

static llvm::cl::list<std::string> VarVec(
    "variable",
    llvm::cl::desc(
        "Refactor the name of a variable or class field."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)
);

static llvm::cl::list<std::string> MacroVec(
    "macro",
    llvm::cl::desc(
        "Refactor a preprocessor macro."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)       
);

static llvm::cl::list<std::string> TagVec(
    "tag",
    llvm::cl::desc(
        "Refactor an enumeration, structure, or class."
    ),
    llvm::cl::value_desc("victim=repl"),
    llvm::cl::CommaSeparated,
    llvm::cl::cat(RefactoringOptions)
);

static llvm::cl::list<std::string> InputFiles(
    llvm::cl::desc("[<file> ...]"),
    llvm::cl::Positional,
    llvm::cl::ZeroOrMore,
    llvm::cl::PositionalEatsArgs
);

static llvm::cl::opt<bool> SyntaxOnly(
    "syntax-only",
    llvm::cl::desc(
        "Perform a syntax check and exit.\n"
        "No changes are made even if replacements were specified."
    ),
    llvm::cl::init(false)
);

static llvm::cl::opt<bool> SanitizeIncludes(
    "sanitize-includes",
    llvm::cl::desc(
        "Find unused included header files and remove them."
    ),
    llvm::cl::cat(RefactoringOptions),
    llvm::cl::init(false)
);

static llvm::cl::opt<std::string> CompileCommandsPath(
    "commands",
    llvm::cl::desc(
        "Specify the <path> to the compilation database\n"
        "(\"compile_commands.json\") for the project.\n"
        "If not specified rf will automatically search all\n"
        "parent directories for such a file."
    ),
    llvm::cl::value_desc("path")
);

static llvm::cl::opt<bool> DryRun(
    "dry-run",
    llvm::cl::desc(
        "Do not make any changes at all.\n"
        "Useful for debugging, especially when used with \"--verbose\"."
    ),
    llvm::cl::cat(FlagOptions),
    llvm::cl::init(false)
);

static llvm::cl::opt<bool> Verbose(
    "verbose",
    llvm::cl::desc(
        "Increase verbosity:\n"
        "Print a line for each replacement to be made."
    ),
    llvm::cl::cat(FlagOptions),
    llvm::cl::init(false)
);

static llvm::cl::opt<bool> Force(
    "force",
    llvm::cl::desc(
        "Disable safety checks and apply replacements even if they may\n"
        "break the code. No replacements are done if \"--dry-run\"\n"
        "is passed along this option."
    ),
    llvm::cl::cat(FlagOptions),
    llvm::cl::init(false)
);

#ifdef __unix__
static llvm::cl::opt<bool> AllowRoot(
    "allow-root",
    llvm::cl::desc(
        "Allow this application to run with root privileges.\n"
    ),
    llvm::cl::cat(FlagOptions),
    llvm::cl::init(false)
);
#endif

/* clang-format on */

static std::unique_ptr<clang::tooling::CompilationDatabase> 
makeCompilationDatabase(const std::string &Path, std::string &ErrMsg)
{
    using namespace clang::tooling;
    
    if (!Path.empty())
        return JSONCompilationDatabase::loadFromFile(Path, ErrMsg);
    
    const char *WorkDir = std::getenv("PWD");
    if (!WorkDir)
        WorkDir = "./";
    
    return CompilationDatabase::autoDetectFromDirectory(WorkDir, ErrMsg);
}


template<typename T> 
static void add(Refactorers &Refactorers, 
                const std::vector<std::string> &ArgVec,
                clang::tooling::Replacements *Repls)
{
    for (const auto &Arg : ArgVec) {
        auto Index = Arg.find('=');
        if (Index == std::string::npos) {
            std::cerr << cl::Error() 
                      << "invalid argument \"" << Arg << "\" - argument syntax "
                      << "is \"Victim=Replacement\"\n";
            std::exit(EXIT_FAILURE);
        }
        
        auto Victim = Arg.substr(0, Index);
        auto Repl = Arg.substr(Index + 1);
        
        auto Refactorer = std::make_unique<T>();
        Refactorer->setReplacements(Repls);
        Refactorer->setVerbose(Verbose);
        Refactorer->setForce(Force);
        
        if (Victim != Repl) {
            Refactorer->setVictimQualifier(std::move(Victim));
            Refactorer->setReplacementQualifier(std::move(Repl));
        }
        
        Refactorers.push_back(std::move(Refactorer));
    }
}

int main(int argc, const char **argv) 
{
    using namespace clang;
    using namespace clang::tooling;

    llvm::cl::ParseCommandLineOptions(argc, argv);
    
#ifdef __unix__
    if (!AllowRoot && getuid() == 0) {
        std::cerr << cl::Error() 
                  << "running on root privileges - aborting...\n";
        std::exit(EXIT_FAILURE);
    }
#endif
    
    auto ErrMsg = std::string();
    auto CompilationDB = makeCompilationDatabase(CompileCommandsPath, ErrMsg);
    if (!CompilationDB) {
        std::cerr << cl::Error() << ErrMsg << "\n";
        std::exit(EXIT_FAILURE);
    }

    auto SourceFiles = CompilationDB->getAllFiles();
    if (!InputFiles.empty())
        std::swap(SourceFiles, *&InputFiles);
    
    if (SyntaxOnly) {
        auto Action = newFrontendActionFactory<clang::SyntaxOnlyAction>();
        
        int err = ClangTool(*CompilationDB, SourceFiles).run(Action.get());
        
        std::exit((err == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    
    auto Refactorers = ::Refactorers();
    
    RefactoringTool Tool(*CompilationDB, SourceFiles);
    auto Repls = &Tool.getReplacements();
    
    add<EnumConstantRefactorer>(Refactorers, EnumConstantVec, Repls);
    add<FunctionRefactorer>(Refactorers, FunctionVec, Repls);
    add<MacroRefactorer>(Refactorers, MacroVec, Repls);
    add<NamespaceRefactorer>(Refactorers, NamespaceVec, Repls);
    add<TagRefactorer>(Refactorers, TagVec, Repls);
    add<VariableRefactorer>(Refactorers, VarVec, Repls);
    
    if (SanitizeIncludes) {
        auto Refactorer = std::make_unique<IncludeRefactorer>();
        Refactorer->setReplacements(Repls);
        Refactorer->setVerbose(Verbose);
        Refactorer->setForce(Force);
        
        Refactorers.push_back(std::move(Refactorer));
    }

    if (!Refactorers.empty()) {
        auto Factory = std::make_unique<RefactoringActionFactory>();
        Factory->setRefactorers(&Refactorers);
        
        int err = Tool.run(Factory.get());
        if (err) {
            std::cerr << cl::Error() 
                      << "found syntax error(s) while refactoring\n";
            
            if (!Force) {
                std::cerr << cl::Info() << "use \"--force\" to override\n";
                std::exit(EXIT_FAILURE);
            }
        }
    }
    
    if (!DryRun) {
        if (Tool.getReplacements().empty()) {
            std::cerr << cl::Info() << "no replacements found - done\n";
            exit(EXIT_SUCCESS);
        }
        
        IntrusiveRefCntPtr<DiagnosticOptions> Opts = new DiagnosticOptions();
        IntrusiveRefCntPtr<DiagnosticIDs> Id = new DiagnosticIDs();
        
        TextDiagnosticPrinter Printer(llvm::errs(), &*Opts);
        DiagnosticsEngine Diagnostics(Id, &*Opts, &Printer, false);
        SourceManager SM(Diagnostics, Tool.getFiles());
        
        Rewriter Rewriter(SM, LangOptions());
        
        bool ok = Tool.applyAllReplacements(Rewriter);
        if (!ok) {
            std::cerr << cl::Error() << "failed to apply all replacements\n";
            std::exit(EXIT_FAILURE);
        }
        
        bool err = Rewriter.overwriteChangedFiles();
        if (err) {
            std::cerr << cl::Error() << "failed to save changes to disk\n";
            std::exit(EXIT_FAILURE);
        }
    }
        
    return EXIT_SUCCESS;
}
