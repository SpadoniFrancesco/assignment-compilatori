#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include <cassert>

using namespace llvm;

namespace {

struct LoopPass : public PassInfoMixin<LoopPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        // Analisi dei loop nella funzione F
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

        if (LI.empty()) { // se non ci sono loop nella funzione
            errs() << "La funzione " << F.getName() << " non contiene loop.\n";
            return PreservedAnalyses::all();
        }

        // scorro tutti i BB della funzione
        for (BasicBlock &BB : F) {
            if (LI.isLoopHeader(&BB)) { //capisco se il BB è un header
                outs() << "--- STAMPA HEADER BB: " << BB.getName() << " ---\n";
                outs() << BB;
                outs() << "---------------------------------------\n";
            }
        }

        // itero sul LoopInfo per ottenere i loop presenti nella funzione
        for (LoopInfo::iterator L = LI.begin(); L != LI.end(); ++L) {
            Loop *LL = *L; // handle al loop corrente

            if (LL->isLoopSimplifyForm()) { //controllo se il loop è in forma normale
                outs() << "Il loop E' in forma normale (Loop Simplify Form).\n";
            } else {
                outs() << "Il loop NON e' in forma normale.\n";
            }

            BasicBlock *HeaderBB = LL->getHeader(); //recuperiamo header
            if (HeaderBB) {
                // Recuperiamo la funzione che contiene l'header (e quindi il loop)
                Function *ParentFunc = HeaderBB->getParent();
                
                outs() << "Funzione recuperata dall'header: " << ParentFunc->getName() << "\n";
                outs() << "--- STAMPA DEL CFG DELLA FUNZIONE ---\n";
                
                // Stampiamo l'intera funzione
                ParentFunc->print(outs());
                
                outs() << "-------------------------------------\n";
            }

            for (Loop::block_iterator BI = LL->block_begin(); BI != LL->block_end(); ++BI) {
                BasicBlock *BB = *BI; // Estraiamo il puntatore al blocco corrente
                
                outs() << "\n--- Blocco del Loop: " << BB->getName() << " ---\n";
                BB->print(outs()); 
            }

        }

        // secondo punto: stampo il Dominance Tree della funzione
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);

        outs() << "\n=========================================\n";
        outs() << "   STRUTTURA DEL DOMINATOR TREE PER: " << F.getName() << "\n";
        outs() << "=========================================\n";

        for (auto *DTN : breadth_first(DT.getRootNode())){
            BasicBlock *BB = DTN->getBlock();
            if (BB) {
                outs() << "Nodo: " << BB->getName();

                // Recuperiamo il Dominatore Immediato (il padre nell'albero)
                DomTreeNode *IDomNode = DTN->getIDom();
                if (IDomNode && IDomNode->getBlock()) {
                    outs() << "   -> Dominato Immediatamente da: " << IDomNode->getBlock()->getName();
                } else {
                    outs() << "   -> (Radice dell'albero)";
                }
                outs() << "\n";
            }
        }
        outs() << "=========================================\n";

        return PreservedAnalyses::all();
    }
};

} // namespace

//-----------------------------------------------------------------------------
// Registrazione del Plugin per il New PM
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLoopPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LoopPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  // opt con il flag "-loop-pass"
                  if (Name == "loop-pass") { 
                    FPM.addPass(LoopPass()); 
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLoopPassPluginInfo();
}