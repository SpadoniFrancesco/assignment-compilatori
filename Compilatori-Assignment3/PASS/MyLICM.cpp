#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <set>
#include <vector>
#include <algorithm>

using namespace llvm;

namespace {

struct LoopPass : public PassInfoMixin<LoopPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        // Analisi dei loop nella funzione F
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
        // calcolo il Dominator Tree della funzione F
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);

        if (LI.empty()) { // se non ci sono loop nella funzione
            errs() << "La funzione " << F.getName() << " non contiene loop.\n";
            return PreservedAnalyses::all();
        }

        for (LoopInfo::iterator L = LI.begin(); L != LI.end(); ++L) {
            Loop *LL = *L; // handle al loop corrente

            bool changed = true;
            std::set<Instruction*> Invariants;

            while (changed) { //finché ci sono cambiamenti, continuo a cercare invarianti
                changed = false;
                for (BasicBlock *BB : LL->getBlocks()){ //iteriamo sui blocchi del loop
                    for (Instruction &I : *BB) {
                        // Saltiamo PHI node e istruzioni che non possono essere rimosse/spostate
                        if (isa<PHINode>(&I) || I.mayHaveSideEffects() || I.isTerminator())
                            continue;
                            
                        // Se l'abbiamo già trovata nei giri scorsi, saltiamola
                        if (Invariants.count(&I))
                            continue;

                        bool isInstInvariant = true;

                        for (auto &Op : I.operands()) { //iteriamo sugli operandi dell'istruzione
                            Value *V = Op.get();
                            
                            // Costante o Argomento della funzione
                            if (isa<Constant>(V) || isa<Argument>(V)) {
                                continue; 
                            }
                            
                            // È un'istruzione?
                            if (Instruction *OpInst = dyn_cast<Instruction>(V)) {
                                // Definita fuori dal loop
                                if (!LL->contains(OpInst->getParent())) {
                                    continue;
                                }
                                // Definita dentro, ma già marcata come invariante
                                if (Invariants.count(OpInst)) {
                                    continue;
                                }
                            }
                            
                            // Se un operando fallisce tutti i controlli, l'istruzione NON è invariante
                            isInstInvariant = false;
                            break;
                        }
                        
                        // Se tutti gli operandi sono invarianti, la aggiungiamo!
                        if (isInstInvariant) {
                            Invariants.insert(&I);
                            changed = true; // Il punto fisso deve continuare!
                        }
                    }
                }
            }

            //creazione del vettore dei candidati al code motion
            std::vector<Instruction*> CodeMotionCandidates;

            for (Instruction *I : Invariants) {
                // Il blocco dell'istruzione domina tutte le uscite?
                BasicBlock *InstBlock = I->getParent();
                SmallVector<BasicBlock*, 4> ExitBlocks; // <-- Cambiato in SmallVector
                LL->getExitBlocks(ExitBlocks);
                
                bool dominatesAllExits = true;

                for (BasicBlock *ExitBB : ExitBlocks) {
                    if (!DT.dominates(InstBlock, ExitBB)) {
                        dominatesAllExits = false;
                        break;
                    }
                }

                if (dominatesAllExits) {
                    bool dominatesAllUsers = true;

                    // Controlliamo se l'istruzione domina tutti i suoi utenti all'interno del loop
                    for (User *U : I->users()) {
                        // Ci interessano solo gli utenti che sono a loro volta istruzioni
                        if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                            
                            // Verifichiamo se l'istruzione che la usa si trova DENTRO il loop
                            BasicBlock *UserBlock = UserInst->getParent();
                            if (LL->contains(UserBlock)) {
                                
                                // Se l'utente è dentro il loop, il blocco di I deve dominarlo!
                                if (!DT.dominates(InstBlock, UserBlock)) {
                                    dominatesAllUsers = false; // <-- Corretto il refuso qui
                                    break; // Condizione fallita per questo utente
                                }
                            }
                        }
                    }

                    if (dominatesAllUsers) {
                        // da spostare!
                        CodeMotionCandidates.push_back(I);
                    }
                }
            }

            // Spostamento nel Preheader
            BasicBlock *Preheader = LL->getLoopPreheader();
            if (Preheader && !CodeMotionCandidates.empty()) {
                Instruction *Terminator = Preheader->getTerminator();
                
                // Insieme per tenere traccia di quali istruzioni abbiamo GIÀ spostato nel preheader
                std::set<Instruction*> MovedInstructions;
                
                outs() << "\n>>> ESECUZIONE CODE MOTION (DFS) <<<\n";
                
                // Usiamo il DFSTraversal di LLVM sui blocchi del loop
                for (BasicBlock *BB : LL->getBlocks()) {
                    
                    // Iteratore esplicito sulle istruzioni del blocco
                    for (auto InstIt = BB->begin(), InstEnd = BB->end(); InstIt != InstEnd; ) {
                        Instruction &I = *InstIt;
                        ++InstIt; // Incrementiamo SUBITO l'iteratore prima di toccare l'istruzione!
                        
                        // Controlliamo se questa istruzione è tra i candidati validati
                        auto it = std::find(CodeMotionCandidates.begin(), CodeMotionCandidates.end(), &I);
                        if (it != CodeMotionCandidates.end()) {
                            
                            // Spostare l’istruzione candidata nel preheader se tutte le istruzioni invarianti da cui questa dipende sono state spostate
                            bool readyToMove = true;
                            for (auto &Op : I.operands()) {
                                if (Instruction *OpInst = dyn_cast<Instruction>(Op.get())) {
                                    // Se l'operando è dentro il loop ed è un'istruzione invariante
                                    if (LL->contains(OpInst->getParent()) && Invariants.count(OpInst)) {
                                        // ...deve essere già stata spostata!
                                        if (!MovedInstructions.count(OpInst)) {
                                            readyToMove = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            if (readyToMove) {
                                outs() << "Sposto nel preheader (Dipendenze OK): ";
                                I.print(outs());
                                outs() << "\n";
                                
                                I.moveBefore(Terminator); // Ora lo spostamento è sicuro!
                                MovedInstructions.insert(&I); 
                            }
                        }
                    }
                }
                outs() << ">>> FINE CODE MOTION <<<\n\n";
            }
        }
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