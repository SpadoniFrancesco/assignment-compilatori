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

        for (LoopInfo::iterator L = LI.begin(); L != LI.end(); ++L) { // iteriamo sui loop della funzione
            Loop *LL = *L; // handle al loop corrente

            bool changed = true;
            std::set<Instruction*> Invariants; // set delle istruzioni invarianti

            while (changed) { // Algoritmo a punto fisso: serve per risolvere le catene di dipendenze tra istruzioni invarianti (es. se B dipende da A, scopriamo che B è invariante solo al giro successivo a quello in cui abbiamo scoperto A)
                changed = false;
                for (BasicBlock *BB : LL->getBlocks()){ //iteriamo sui blocchi del loop
                    for (Instruction &I : *BB) {
                        // Saltiamo PHI node e istruzioni che non possono essere rimosse/spostate
                        if (isa<PHINode>(&I) || I.mayHaveSideEffects() || I.isTerminator())
                            continue;
                            
                        // Se l'abbiamo già trovata nei giri scorsi, saltiamola
                        if (Invariants.count(&I))
                            continue;

                        bool isInstInvariant = true; // ipotizziamo che l'istruzione sia sempre invariante

                        for (auto &Op : I.operands()) { //iteriamo sugli operandi dell'istruzione
                            Value *V = Op.get();
                            
                            // Costante o Argomento della funzione
                            if (isa<Constant>(V) || isa<Argument>(V)) {
                                continue; 
                            }
                            
                            // È un'istruzione?
                            if (Instruction *OpInst = dyn_cast<Instruction>(V)) {
                                // Definita fuori dal loop, quindi il suo valore non cambia durante le iterazioni del loop
                                if (!LL->contains(OpInst->getParent())) {
                                    continue;
                                }
                                // L'istruzione che definisce quel valore è dentro il loop, se tale istruzione e' invariante allora lo sara' anche questa istruzione
                                if (Invariants.count(OpInst)) {
                                    continue;
                                }
                            }
                            
                            isInstInvariant = false;
                            break;
                        }
                        
                        // Se tutti gli operandi sono invarianti, la aggiungiamo!
                        if (isInstInvariant) {
                            Invariants.insert(&I);
                            changed = true;
                        }
                    }
                }
            }

            // vettore dei candidati al code motion
            std::vector<Instruction*> CodeMotionCandidates;

            // Il blocco dell'istruzione domina tutte le uscite?
            for (Instruction *I : Invariants) {
                BasicBlock *InstBlock = I->getParent(); // Prendiamo il basickblock dell'istruzione
                SmallVector<BasicBlock*, 4> ExitBlocks;
                LL->getExitBlocks(ExitBlocks); // Prendiamo tutti i blocchi di uscita del loop
                
                bool dominatesAllExits = true;

                for (BasicBlock *ExitBB : ExitBlocks) {
                    if (!DT.dominates(InstBlock, ExitBB)) { // Il blocco dell'istruzione non domina un blocco di uscita
                        dominatesAllExits = false;
                        break;
                    }
                }

                // Domina anche tutti gli utenti interni al loop?
                if (dominatesAllExits) {
                    bool dominatesAllUsers = true;

                    for (User *U : I->users()) {
                        // Ci interessano solo gli utenti che sono a loro volta istruzioni
                        if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
                            
                            // Verifichiamo se l'istruzione che la usa si trova DENTRO il loop
                            BasicBlock *UserBlock = UserInst->getParent();
                            if (LL->contains(UserBlock)) {
                                
                                // Se l'utente è dentro il loop, il blocco di I deve dominarlo!
                                if (!DT.dominates(InstBlock, UserBlock)) {
                                    dominatesAllUsers = false;
                                    break; 
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
                Instruction *Terminator = Preheader->getTerminator(); // prendiamo il terminator del preheader per spostare le istruzioni prima di lui
                
                // Set in cui tentiamo traccia di quali istruzioni abbiamo GIÀ spostato nel preheader
                std::set<Instruction*> MovedInstructions;
                
                outs() << "\n>>> ESECUZIONE CODE MOTION <<<\n";
                
                for (BasicBlock *BB : LL->getBlocks()) {
                    
                    // Iteriamo sulle istruzioni del blocco
                    for (auto InstIt = BB->begin(), InstEnd = BB->end(); InstIt != InstEnd; ) {
                        Instruction &I = *InstIt;
                        ++InstIt; // Incrementiamo qui l'iteratore per evitare problemi se spostiamo l'istruzione e invalidiamo l'iteratore
                        
                        // Controlliamo se questa istruzione è tra i candidati alla code motion
                        auto it = std::find(CodeMotionCandidates.begin(), CodeMotionCandidates.end(), &I);
                        if (it != CodeMotionCandidates.end()) { // se non è tra i candidati find ci restitiusce .end()
                            
                            // Spostare l’istruzione candidata nel preheader se tutte le istruzioni invarianti da cui questa dipende sono state spostate
                            bool readyToMove = true;
                            for (auto &Op : I.operands()) {
                                // Prendiamo l'istruzione che definisce l'operando
                                if (Instruction *OpInst = dyn_cast<Instruction>(Op.get())) {
                                    // Se l'operando è dentro il loop ed è un'istruzione invariante
                                    if (LL->contains(OpInst->getParent()) && Invariants.count(OpInst)) {
                                        // Se l'operando non è stato ancora spostato, non possiamo spostare questa istruzione
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
                                
                                I.moveBefore(Terminator); // Spostiamo prima del terminator
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