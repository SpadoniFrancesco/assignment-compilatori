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
#include <iostream>

using namespace llvm;

namespace {

struct MyLoopFusion: public PassInfoMixin<MyLoopFusion> {
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
          return;
      }
      
      // Identifichiamo i blocchi chiave dei due loop
      BasicBlock *HeaderL0 = L0->getHeader();
      BasicBlock *LatchL0  = L0->getLoopLatch();
      BasicBlock *HeaderL1 = L1->getHeader();
      BasicBlock *LatchL1  = L1->getLoopLatch();

      // AGGIORNIAMO LE PHI DI HEADER L0 PRIMA DI ALTERARE IL CFG
      for (PHINode &PN : HeaderL0->phis()) {
          int LatchL0Idx = PN.getBasicBlockIndex(LatchL0);
          if (LatchL0Idx != -1) {
              Value *LoopNextVal = PN.getIncomingValue(LatchL0Idx);
              PN.removeIncomingValue(LatchL0Idx, false);
              PN.addIncoming(LoopNextVal, LatchL1); // Il valore incrementato ora tornerà dal Latch di L1!
          }
      }

      // Rimuoviamo il vecchio terminatore
      Instruction *TermL0 = LatchL0->getTerminator();
      TermL0->eraseFromParent();
      // Creiamo un salto dritto e incondizionato verso il corpo del secondo loop
      BranchInst::Create(HeaderL1, LatchL0); 
      
      // AGGIORNIAMO LE PHI DI HEADER L1
      for (PHINode &PN : HeaderL1->phis()) {
          int Index = PN.getBasicBlockIndex(L1->getLoopPreheader());
          if (Index != -1) {
              PN.setIncomingBlock(Index, LatchL0);
          }
      }

      // COLLEGHIAMO IL RITORNO (Il Latch di L1 ora torna all'inizio di tutto: HeaderL0)
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

      llvm::errs() << "Loop Fusion completata con successo per L0 e L1.\n";
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    llvm::errs() << "--- ENTRATO IN RUN PER LA FUNZIONE: " << F.getName() << " ---\n";
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

    // creazione della worklist dei loop da processare, prendiamo i loop dal piu' interno al piu' esterno
    SmallVector<Loop *, 8> Worklist;
    for (Loop *TopLevelLoop : LI) {
        for (Loop *L : depth_first(TopLevelLoop))
        // Prendiamo solo i loop "foglia".
        if (L->isInnermost())
        Worklist.push_back(L);
    }

    llvm::errs() << "[DEBUG] Numero di loop innermost trovati nella worklist: " << Worklist.size() << "\n";

    for (Loop *L0 : Worklist) {

        if (ErasedLoops.count(L0)) { // controlliamo se il loop L0 è già stato fuso in precedenza
            continue;
        }
        if (!L0->isLoopSimplifyForm()) { // controlliamo se il loop L0 è in forma semplificata
            continue;
        }

        // Prendiamo l'uscita di L0
        BasicBlock *ExitL0 = L0->getExitBlock();
        if (!ExitL0) { // controlliamo se ha uscite multiple o condizionali
            continue;
        }

        // Controlliamo se il loop ha un unico successore.
        BasicBlock *PossibleHeaderL1 = ExitL0->getUniqueSuccessor();
        if (!PossibleHeaderL1) {
            continue;
        }

        // Chiediamo a LoopInfo (LI) se quel blocco appartiene a un loop
        Loop *L1 = LI.getLoopFor(PossibleHeaderL1);

        if (!L1) {
            continue;
        }

        // Verifichiamo che L1 esista, che L1 sia diverso da L0 e sia in forma semplificata o gia' fuso.
        if (L1 == L0) {
            llvm::errs() << "  -> Controllo Fallito: L1 coincide con L0 (auto-riferimento).\n";
            continue;
        }
        if (!L1->isLoopSimplifyForm()) {
            llvm::errs() << "  -> Controllo Fallito: L1 NON è in Loop Simplify Form.\n";
            continue;
        }
        if (ErasedLoops.count(L1)) {
            llvm::errs() << "  -> Salto L1: è già stato fuso in precedenza.\n";
            continue;
        }

        // Estraiamo gli Header dei due loop e verifichiamo le relazioni di dominanza
        BasicBlock *HeaderL0 = L0->getHeader();
        BasicBlock *HeaderL1 = L1->getHeader();
        bool L0DominatesL1 = DT.dominates(HeaderL0, HeaderL1);
        bool L1PostDominatesL0 = PDT.dominates(HeaderL1, HeaderL0);

        if (!L0DominatesL1 || !L1PostDominatesL0) { // sono control flow equivalent?
            continue;
        }

        // Calcolo del trip count dei due loop usando Scalar Evolution
        const SCEV *TripCountL0 = SE.getBackedgeTakenCount(L0);
        const SCEV *TripCountL1 = SE.getBackedgeTakenCount(L1);

        // SCEV è riuscito a calcolarli?
        if (isa<SCEVCouldNotCompute>(TripCountL0) || isa<SCEVCouldNotCompute>(TripCountL1)) {
            continue; 
        }

        // Confronto dei trip count dei due loop
        if (TripCountL0 != TripCountL1) {
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

                // Se l'analisi non è consistente, siamo conservativi
                if (!DepResult->isConsistent()) {
                    llvm::errs() << "  -> FUSION FALLITA: Rilevata dipendenza NON consistente tra istruzioni.\n";
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
                    llvm::errs() << "  -> FUSION FALLITA: Trovata dipendenza a distanza negativa (Direzione GT '>').\n";
                    HasNegativeDistanceDependence = true;
                    break;
                }
            }
            if (HasNegativeDistanceDependence) break;
        }

        // E' stata trovata una dipendenza negativa, saltiamo questa coppia di loop
        if (HasNegativeDistanceDependence) {
            continue;
        } else { // controlli superati, possiamo fondere i loop
            FondiLoop(L0, L1, LI, DT);
            ErasedLoops.insert(L1);
        }
    }

    if (!ErasedLoops.empty()) {
        // Abbiamo fuso i loop, Comunichiamo a LLVM che l'IR è cambiato 
        return PreservedAnalyses::none(); 
    }
    
    // Se non è stato fuso nulla, le analisi precedenti sono ancora valide
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
                  if (Name == "my-loop-fusion") {
                    FPM.addPass(MyLoopFusion());
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