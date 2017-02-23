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

#include <utility>

#include <clang/Lex/Preprocessor.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PTHManager.h>

#include <PPCallbackDispatcher.hpp>
#include <RefactoringASTConsumer.hpp>
#include <RefactoringActionFactory.hpp>

#include <util/memory.hpp>
#include <util/CommandLine.hpp>


void RefactoringAction::setRefactorers(
    std::vector<std::unique_ptr<Refactorer>> *Refactorers)
{
    Refactorers_ = Refactorers;
}

bool RefactoringAction::BeginSourceFileAction(clang::CompilerInstance &CI, 
                                              llvm::StringRef File)
{
    for (auto &Refactorer : *Refactorers_) {
        Refactorer->setCompilerInstance(&CI);
        Refactorer->beforeSourceFileAction(File);
    }
    
    auto Dispatcher = std::make_unique<PPCallbackDispatcher>();
    Dispatcher->setRefactorers(Refactorers_);
        
    CI.getPreprocessor().addPPCallbacks(std::move(Dispatcher));
    
    return true;
}

void RefactoringAction::EndSourceFileAction()
{
    for (auto &Refactorer : *Refactorers_)
        Refactorer->afterSourceFileAction();
}

// void RefactoringAction::ExecuteAction()
// {
//     auto &CI = getCompilerInstance();
//     auto &SM = CI.getSourceManager();
//     
//     auto File = getCurrentFile().str();
//     while (File.back() != '.')
//         File.pop_back();
//     
//     File += "pth";
//     
//     llvm::errs() << "Looking for: " << File << "\n";
//     
//     auto FileEntry = SM.getFileManager().getFile(File);
//     
//     if (FileEntry && File != "src/util/memory.pth") {
//         auto FileID = SM.getOrCreateFileID(FileEntry, clang::SrcMgr::C_User);
//         
//         auto &PP = CI.getPreprocessor();
//         
//         auto PTHManager = clang::PTHManager::Create(File, CI.getDiagnostics());
//         if (PTHManager)
//             PP.setPTHManager(PTHManager);
// //         auto Lexer = PTHManager->CreateLexer(FileID);
//         
//         
// //         clang::Token Token;
// //         
// //         do {
// //             bool Ok = Lexer->Lex(Token);
// //             if (!Ok) {
// //                 llvm::errs() << util::cl::Error() 
// //                 << "failed to parse \"" << File << "\"\n";
// //             }
// //         } while (Token.isNot(clang::tok::eof));
// //         
// //         
// //         delete Lexer;
//     }
//     
//     clang::ASTFrontendAction::ExecuteAction();
//     
//     
//     auto OutFile = CI.createDefaultOutputFile(true, File, ".pth");
//     if (!OutFile)
//         return;
//     
//     clang::CacheTokens(CI.getPreprocessor(), OutFile.get());
// }

std::unique_ptr<clang::ASTConsumer> 
RefactoringAction::CreateASTConsumer(clang::CompilerInstance &CI, 
                                     llvm::StringRef File)
{
    (void) CI;
    (void) File;
    
    auto Consumer = std::make_unique<RefactoringASTConsumer>();
    Consumer->setRefactorers(Refactorers_);

    return Consumer;
}

std::vector<std::unique_ptr<Refactorer>> &
RefactoringActionFactory::refactorers()
{
    return Refactorers_;
}

const std::vector<std::unique_ptr<Refactorer>> &
RefactoringActionFactory::refactorers() const
{
    return Refactorers_;
}

clang::FrontendAction *RefactoringActionFactory::create()
{
    if (Refactorers_.empty())
        return new clang::SyntaxOnlyAction();
        
    auto Action = new RefactoringAction();
    Action->setRefactorers(&Refactorers_);
    
    return Action;
}
