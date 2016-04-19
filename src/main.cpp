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

#include <Refactorers/TagRefactorer.hpp>
#include <Refactorers/FunctionRefactorer.hpp>
#include <Refactorers/IncludeRefactorer.hpp>
#include <Refactorers/NamespaceRefactorer.hpp>
#include <Refactorers/VariableRefactorer.hpp>
#include <Refactorers/MacroRefactorer.hpp>
#include <util/memory.hpp>

#include <RefactoringActionFactory.hpp>

static llvm::cl::OptionCategory RefactoringOptions("Code refactoring options");
static llvm::cl::OptionCategory FlagOptions("Flags");
static llvm::cl::extrahelp HelpText(
    "\n!! Commit your source code to a version control system before "
    "refactoring it !!\n\n"
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

static llvm::cl::list<std::string> InputFiles(
    llvm::cl::desc("[File01 [File02 [...]]]"),
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

static llvm::cl::opt<std::string> CompDBPath(
    "comp-db",
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
static void addRefactorers(RefactorerVector &Refactorers,
                           const std::vector<std::string> &ArgVec, 
                           clang::tooling::Replacements *Repls)
{
    for (const auto &Str : ArgVec) {
        auto Pos = Str.find('=');
        if (Pos == std::string::npos) {
            std::cerr << "** ERROR: invalid argument '" << Str << "' - have a "
                      << "look at the help message" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        
        auto VictimName = Str.substr(0, Pos);
        auto ReplName = Str.substr(Pos + sizeof(char));

        auto Refactorer = std::make_unique<T>();
        Refactorer->setReplacements(Repls);
        Refactorer->setVictimQualifier(std::move(VictimName));
        Refactorer->setReplacementQualifier(std::move(ReplName));
        Refactorer->setVerbose(Verbose);
        Refactorer->setForce(Force);
        
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
        std::cerr << "** ERROR: running on root privileges - aborting...\n";
        std::exit(EXIT_FAILURE);
    }
#endif
    
    auto ErrMsg = std::string();
    auto CompilationDB = makeCompilationDatabase(CompDBPath, ErrMsg);
    if (!CompilationDB) {
        std::cerr << "** ERROR: " << ErrMsg << std::endl;
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
        
    auto Tool = RefactoringTool(*CompilationDB, SourceFiles);
    auto Repls = &Tool.getReplacements();
    
    auto RefactorerVec = RefactorerVector();
    
    addRefactorers<TagRefactorer>(RefactorerVec, TagVec, Repls);
    addRefactorers<FunctionRefactorer>(RefactorerVec, FunctionVec, Repls);
    addRefactorers<NamespaceRefactorer>(RefactorerVec, NamespaceVec, Repls);
    addRefactorers<VariableRefactorer>(RefactorerVec, VarVec, Repls);
    addRefactorers<MacroRefactorer>(RefactorerVec, MacroVec, Repls);
    
    if (SanitizeIncludes) {
        auto Refactorer = std::make_unique<IncludeRefactorer>();
        Refactorer->setReplacements(Repls);
        Refactorer->setVerbose(Verbose);
        Refactorer->setForce(Force);
        
        RefactorerVec.push_back(std::move(Refactorer));
    }

    if (!RefactorerVec.empty()) {
        auto Factory = std::make_unique<RefactoringActionFactory>();
        Factory->setRefactorers(&RefactorerVec);
        
        int err = Tool.run(Factory.get());
        if (err != 0) {
            std::cerr << "** ERROR: error(s) generated while refactoring\n";
            
            if (!Force) {
                std::cerr << "** INFO: use \"--force\" if you still want to "
                          << "apply replacements\n";
                std::exit(EXIT_FAILURE);
            }
        }
    }
    
    if (Tool.getReplacements().empty()) {
        std::cerr << "** Info: no code replacements to make - done\n";
    } else if (!DryRun) {
        IntrusiveRefCntPtr<DiagnosticOptions> Opts = new DiagnosticOptions();
        IntrusiveRefCntPtr<DiagnosticIDs> Id = new DiagnosticIDs();
        
        TextDiagnosticPrinter Printer(llvm::errs(), &*Opts);
        DiagnosticsEngine Diagnostics(Id, &*Opts, &Printer, false);
        SourceManager SM(Diagnostics, Tool.getFiles());
        
        Rewriter Rewriter(SM, LangOptions());
        
        bool ok = Tool.applyAllReplacements(Rewriter);
        if (!ok) {
            std::cerr << "** ERROR: failed to apply all code replacements\n";
            std::exit(EXIT_FAILURE);
        }
        
        bool err = Rewriter.overwriteChangedFiles();
        if (err) {
            std::cerr << "** ERROR: failed to save changes to disk\n";
            std::exit(EXIT_FAILURE);
        }
    }
        
    return 0;
}
