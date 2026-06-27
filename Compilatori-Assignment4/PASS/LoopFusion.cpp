#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <set>
#include <vector>
#include <algorithm>

using namespace llvm;

namespace {

struct LoopFusion: public PassInfoMixin<LoopFusion> {
  void FondiLoop(Loop *L0, Loop *L1, LoopInfo &LI, DominatorTree &DT) {
      Function *F = L0->getHeader()->getParent();
      
      // Prendiamo l'istruzione PHINode che fa da contatore per i due cicli.
      PHINode *IndVarL0 = L0->getCanonicalInductionVariable();
      PHINode *IndVarL1 = L1->getCanonicalInductionVariable();

      if (IndVarL0 && IndVarL1) {
          // Sostituiamo ogni uso della induction variable di L1 con quella di L0
          // all'interno del corpo del secondo loop.
          IndVarL1->replaceAllUsesWith(IndVarL0);
          
          // Rimuoviamo l'istruzione PHI obsoleta di L1 dal suo header
          IndVarL1->eraseFromParent();
      } else {
          dbgs() << "Attenzione: Impossibile trovare la induction variable canonica.\n";
          return;
      }
      
      // Identifichiamo i blocchi chiave dei due loop
      BasicBlock *HeaderL0 = L0->getHeader();
      BasicBlock *LatchL0  = L0->getLoopLatch();
      BasicBlock *HeaderL1 = L1->getHeader();
      BasicBlock *LatchL1  = L1->getLoopLatch();

      Instruction *TermL0 = LatchL0->getTerminator(); // prendiamo l'istruzione di salto
      BranchInst::Create(HeaderL1, LatchL0); // Crea un salto diretto 'br label %loop1.header'
      TermL0->eraseFromParent();
      
      // Scansioniamo le possibili destinazioni di quel salto. Quando troviamo il salto che tornava indietro a HeaderL0, lo scolleghiamo e lo attacchiamo a HeaderL1
      for (PHINode &PN : HeaderL1->phis()) {
          int Index = PN.getBasicBlockIndex(L1->getLoopPreheader());
          if (Index != -1) {
              // Il flusso che prima arrivava dal preheader di L1 ora arriva dal Latch di L0!
              PN.setIncomingBlock(Index, LatchL0);
          }
      }

      // colleghiamo la fine di L1 (il suo LatchL1) all'indietro verso l'inizio di tutto (HeaderL0)
      Instruction *TermL1 = LatchL1->getTerminator();
      for (unsigned i = 0; i < TermL1->getNumSuccessors(); ++i) {
          if (TermL1->getSuccessor(i) == HeaderL1) {
              TermL1->setSuccessor(i, HeaderL0);
          }
      }

      // Spostiamo fisicamente i BasicBlock di L1 dentro la struttura di L0
      std::vector<BasicBlock *> BlocksL1 = L1->getBlocks();
      for (BasicBlock *BB : BlocksL1) {
          L0->addBasicBlockToLoop(BB, LI);
      }

      // Rimuoviamo L1 dalla LoopInfo, poiché è stato fuso in L0
      LI.erase(L1);

      // Aggiorniamo il Dominator Tree per riflettere le modifiche al CFG
      DT.recalculate(*F);

      dbgs() << "Loop Fusion completata con successo per L0 e L1.\n";
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    llvm::errs() << "--- Sto analizzando la funzione: " << F.getName() << " ---\n";
    // Analisi dei loop nella funzione F
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
    // calcolo il Dominator Tree della funzione F
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
    // calcolo il PostDominator Tree della funzione F
    PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
    // calcolo il Scalar Evolution della funzione F
    ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    // Dependency analysis per i loop
    DependenceInfo &DI =AM.getResult<DependenceAnalysis>(F);

    // Registro dei loop eliminati per evitare Segmentation Fault
    std::set<Loop *> ErasedLoops;

    // creazione della worklist dei loop da processare (Loop Versioning)
    SmallVector<Loop *, 8> Worklist;
    for (Loop *TopLevelLoop : LI) {
        for (Loop *L : depth_first(TopLevelLoop))
        // We only handle inner-most loops.
        if (L->isInnermost())
        Worklist.push_back(L);
    }

    for (Loop *L0 : Worklist) {
        if (ErasedLoops.count(L0)) continue; // Se L0 è già stato fuso in precedenza avanziamo al prossimo loop
        if (!L0->isLoopSimplifyForm()) continue;

        // Prendiamo l'uscita di L0
        BasicBlock *ExitL0 = L0->getExitBlock();
        if (!ExitL0) continue;

        // Nel caso non-guarded, l'ExitBlock di L0 è il Preheader di L1.
        // Quindi l'Header di L1 sarà il successore unico di questo blocco.
        BasicBlock *PossibleHeaderL1 = ExitL0->getUniqueSuccessor();
        if (!PossibleHeaderL1) continue;

        // Chiediamo a LoopInfo (LI) se quel blocco appartiene a un loop
        Loop *L1 = LI.getLoopFor(PossibleHeaderL1);

        // Verifichiamo che L1 esista, che L1 sia diverso da L0 e sia in forma semplificata
        if (L1 && L1 != L0 && L1->isLoopSimplifyForm()) {
            if (ErasedLoops.count(L1)) continue; //Salta se anche L1 è già stato fuso

            // Estraiamo gli Header dei due loop e verifichiamo le relazioni di dominanza
            BasicBlock *HeaderL0 = L0->getHeader();
            BasicBlock *HeaderL1 = L1->getHeader();
            bool L0DominatesL1 = DT.dominates(HeaderL0, HeaderL1);
            bool L1PostDominatesL0 = PDT.dominates(HeaderL1, HeaderL0);

            if (L0DominatesL1 && L1PostDominatesL0) { // I loop sono control flow equivalent (se esegue uno esegue anche l'altro)
                // Calcolo del trip count dei due loop usando Scalar Evolution (numero totale di volte in cui il corpo di un loop viene eseguito prima che il loop termini)
                const SCEV *TripCountL0 = SE.getBackedgeTakenCount(L0);
                const SCEV *TripCountL1 = SE.getBackedgeTakenCount(L1);

                // SCEV è riuscito a calcolarli?
                if (isa<SCEVCouldNotCompute>(TripCountL0) || isa<SCEVCouldNotCompute>(TripCountL1)) {
                    // Non è possibile determinare o garantire il trip count
                    dbgs() << "FUSION FALLITA: SCEV non calcolabile\n";
                    continue; 
                }

                // Confronto dei trip count dei due loop, in questo caso possiamo fare il confronto tra puntatori, in quanto SCEV è un oggetto immutabile e quindi se due trip count sono uguali, i puntatori saranno uguali.
                if (TripCountL0 != TripCountL1) {
                    dbgs() << "FUSION FALLITA: Trip count diversi simbolicamente\n";
                    // STAMPA DEI VALORI PER VEDERE COSA VEDE SCEV:
                    dbgs() << "L0 TC: " << *TripCountL0 << "\n";
                    dbgs() << "L1 TC: " << *TripCountL1 << "\n";
                    continue;
                }

                // Inseriamo le istruzioni di memoria di L0 e L1 in due vettori distinti
                SmallVector<Instruction *, 32> MemInstsL0;
                SmallVector<Instruction *, 32> MemInstsL1;

                // Raccogliamo le istruzioni di memoria da L0
                for (BasicBlock *BB : L0->getBlocks()) {
                    for (Instruction &I : *BB) {
                        if (I.mayReadOrWriteMemory()) {
                            MemInstsL0.push_back(&I);
                        }
                    }
                }

                // Raccogliamo le istruzioni di memoria da L1
                for (BasicBlock *BB : L1->getBlocks()) {
                    for (Instruction &I : *BB) {
                        if (I.mayReadOrWriteMemory()) {
                            MemInstsL1.push_back(&I);
                        }
                    }
                }

                bool HasNegativeDistanceDependence = false;

                // Doppio ciclo per verificare ogni coppia di istruzioni di memoria
                for (Instruction *I0 : MemInstsL0) {
                    for (Instruction *I1 : MemInstsL1) {
                        
                        // Chiediamo l'analisi a DependenceInfo (DI)
                        auto DepResult = DI.depends(I0, I1, true);
                        
                        // Se non c'è dipendenza, la coppia è sicura
                        if (!DepResult) continue;

                        // Se l'analisi non è consistente (puntatori complessi), siamo conservativi e blocchiamo la fusione
                        if (!DepResult->isConsistent()) {
                            HasNegativeDistanceDependence = true;
                            break;
                        }

                        // Le dipendenze indipendenti dal loop non sono a distanza negativa
                        if (DepResult->isLoopIndependent()) continue;

                        // Interroghiamo la direzione al livello di annidamento corrente di L0
                        unsigned Level = L0->getLoopDepth();
                        unsigned Direction = DepResult->getDirection(Level);

                        // Se la direzione contiene GREATER (>), la dipendenza è a distanza negativa
                        if (Direction & Dependence::DVEntry::GT) {
                            HasNegativeDistanceDependence = true;
                            break;
                        }
                    }
                    if (HasNegativeDistanceDependence) break;
                }

                // 4. Se è stata trovata una dipendenza negativa, saltiamo questa coppia di loop
                if (HasNegativeDistanceDependence) {
                    continue;
                }else {
                    FondiLoop(L0, L1, LI, DT);
                    ErasedLoops.insert(L1);
                }
                
            }
        }
    }

    if (!ErasedLoops.empty()) {
        PreservedAnalyses PA;
        PA.preserveSet<CFGAnalyses>(); // Preserva solo le analisi generiche del CFG se aggiornate, ma invalida il resto (come ScalarEvolution)
        return PA;
    }
    return PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};

} // namespace

//-----------------------------------------------------------------------------
// Registrazione del Plugin per il New PM
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLoopFusionPluginInfo() {
  llvm::errs() << "--- PLUGIN LOOP FUSION CARICATO CON SUCCESSO ---\n";
  return {LLVM_PLUGIN_API_VERSION, "LoopFusion", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "loop-fusion") {
                    FPM.addPass(LoopFusion());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLoopFusionPluginInfo();
}