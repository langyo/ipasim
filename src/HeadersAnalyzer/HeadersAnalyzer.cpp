// HeadersAnalyzer.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Parse/ParseAST.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/GlobalDecl.h>
#include <clang/CodeGen/ModuleBuilder.h>
#include <llvm/Analysis/OptimizationDiagnosticInfo.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/GlobalISel/CallLowering.h>
#include <llvm/CodeGen/GlobalISel/MachineIRBuilder.h>
#include <llvm/CodeGen/CallingConvLower.h>
#include <llvm/CodeGen/SelectionDAGISel.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/CodeGen/SelectionDAGISel.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetLowering.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/LegacyPassManager.h>
#include <yaml-cpp/yaml.h>

// Private header files.
#include <llvm/lib/Target/ARM/ARMISelLowering.h>
#include <llvm/lib/CodeGen/SelectionDAG/SelectionDAGBuilder.h>

using namespace clang;
using namespace frontend;
using namespace std;

class HeadersAnalyzer {
public:
    HeadersAnalyzer(CompilerInstance &ci) : ci_(ci) {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();

        cg_ = CreateLLVMCodeGen(ci_.getDiagnostics(), "", ci_.getHeaderSearchOpts(),
            ci_.getPreprocessorOpts(), ci_.getCodeGenOpts(), ctx_); // TODO: this pointer must be deleted!
    }
    void Initialize() {
        cg_->Initialize(ci_.getASTContext()); // TODO: just a wild guess, is it necessary?
    }
    void HandleTopLevelDecl(DeclGroupRef d) {
        cg_->HandleTopLevelDecl(d);
        // TODO: maybe traverse declarations here and clear state of CodeGenerator somehow afterwards
    }
    void VisitFunction(FunctionDecl &f) {
        // dump the function's name and location
        f.printName(llvm::outs());
        llvm::outs() << " ";
        f.getLocation().print(llvm::outs(), f.getASTContext().getSourceManager());
        llvm::outs() << "\n";

        // dump function's type
        auto ft = f.getFunctionType();
        ft->dump(llvm::outs());
        llvm::outs() << "\n";

        // TODO: check that the function is actually exported from the corresponding
        // .dylib file (it's enough to check .tbd file inside the SDK which is simply
        // a YAML)
        //YAML::LoadFile("test.yaml");
        // TODO: maybe use LLVM YAML I/O library instead (http://llvm.org/docs/YamlIO.html)

        // TODO: also check that the function has the same signature in WinObjC headers
        // inside the (NuGet) packages folder
    }
private:
    llvm::LLVMContext ctx_;
    CompilerInstance &ci_;
    CodeGenerator *cg_;
};

class CustomASTVisitor : public RecursiveASTVisitor<CustomASTVisitor> {
public:
    CustomASTVisitor(HeadersAnalyzer &ha) : ha_(ha) {}
    bool VisitFunctionDecl(FunctionDecl *f) { // TODO: override
        ha_.VisitFunction(*f);
        return true;
    }
private:
    HeadersAnalyzer & ha_;
};

class CustomASTConsumer : public ASTConsumer {
public:
    CustomASTConsumer(HeadersAnalyzer &ha) : v_(ha), ha_(ha) {}
    bool HandleTopLevelDecl(DeclGroupRef d) override {
        ha_.HandleTopLevelDecl(d);
        // TODO: move the following code into the HandleTopLevelDecl function
        for (auto b : d) {
            v_.TraverseDecl(b);
        }
        return true;
    }
private:
    CustomASTVisitor v_;
    HeadersAnalyzer &ha_;
};

int main()
{
    // inspired by https://github.com/loarabia/Clang-tutorial/
    // TODO: move this to a separate class

    CompilerInstance ci;
    ci.createDiagnostics();
    ci.getDiagnostics().setIgnoreAllWarnings(true);

    //ci.getHeaderSearchOpts().Sysroot = "C:/Users/Jones/Files/Projects/IPASimulator/deps/headers/iPhoneOS11.1.sdk/";
    ci.getHeaderSearchOpts().AddPath("C:/Users/Jones/Files/Projects/IPASimulator/deps/headers/iPhoneOS11.1.sdk/System/Library/Frameworks/", IncludeDirGroup::Angled, /*IsFramework*/ true, /*IgnoreSysRoot*/ false);
    ci.getHeaderSearchOpts().AddPath("C:/Users/Jones/Files/Projects/IPASimulator/deps/headers/iPhoneOS11.1.sdk/usr/include/", IncludeDirGroup::Angled, /*IsFramework*/ false, /*IgnoreSysRoot*/ false);
    ci.getHeaderSearchOpts().AddPath("C:/Users/Jones/Files/Projects/IPASimulator/deps/clang/lib/Headers/", IncludeDirGroup::Angled, /*IsFramework*/ false, /*IgnoreSysRoot*/ false);
    //ci.getHeaderSearchOpts().ResourceDir = "C:/Users/Jones/Files/Projects/IPASimulator/deps/clang/lib/Headers/";

    auto targetOpts = make_shared<TargetOptions>();
    targetOpts->Triple = "arm-apple-darwin"; // TODO: just a wild guess
    ci.setTarget(TargetInfo::CreateTargetInfo(ci.getDiagnostics(), targetOpts)); // TODO: TargetInfo* should be deleted when not needed anymore

    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());

    ci.getInvocation().setLangDefaults(ci.getLangOpts(), InputKind::ObjC, ci.getTarget().getTriple(), ci.getPreprocessorOpts());
    ci.getLangOpts().Blocks = 1;

    //ci.getPreprocessorOpts().UsePredefines = false;
    ci.createPreprocessor(TranslationUnitKind::TU_Complete);

    HeadersAnalyzer ha(ci);
    ci.setASTConsumer(make_unique<CustomASTConsumer>(ha));
    ci.createASTContext();
    ha.Initialize();
    ci.createSema(TranslationUnitKind::TU_Complete, nullptr);

    const auto file = ci.getFileManager().getFile("C:/Users/Jones/Files/Projects/IPASimulator/deps/headers/iPhoneOS11.1.sdk/System/Library/Frameworks/Foundation.framework/Headers/Foundation.h");
    ci.getSourceManager().setMainFileID(ci.getSourceManager().createFileID(file, SourceLocation(), SrcMgr::C_User));

    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(), &ci.getPreprocessor());
    ParseAST(ci.getSema(), /*PrintStats*/ true, /*SkipFunctionBodies*/ true);
    ci.getDiagnosticClient().EndSourceFile();

    return 0;
}
