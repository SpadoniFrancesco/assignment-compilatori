#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include <cassert>

using namespace llvm;

namespace {

struct LocalOpts : public PassInfoMixin<LocalOpts> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    bool Modified = false;

    for (BasicBlock &B : F) {
        for (auto InstIter = B.begin(); InstIter != B.end(); ++InstIter) {
            Instruction &I = *InstIter;

            switch (I.getOpcode()) {
            case Instruction::Mul: { // Istruzione = MUL
            Value *Op0 = I.getOperand(0);
            Value *Op1 = I.getOperand(1);

            // =========================================================================
            // CASO 1: La costante della MUL è a SINISTRA (Op0)
            // =========================================================================
            if (ConstantInt *CI = dyn_cast<ConstantInt>(Op0)) { // DYNCAST per controllare che sia una costante intera
                const APInt &Val = CI->getValue(); 
                Value *Variable = Op1; 

                if (Val == 1) { // Identità algebrica basica (x * 1 = x)
                    I.replaceAllUsesWith(Op1); //Rimpiazziamo le istruzioni che usano la MUL con Op1
                    Instruction *DeadInst = &I;
                    --InstIter;
                    DeadInst->eraseFromParent(); //Eliminiamo la MUL
                    Modified = true;
                } else if (!dyn_cast<ConstantInt>(Op1)) { // Se Op1 non è una costante, possiamo provare a fare strength reduction
                    unsigned ShiftAmount = Val.ceilLogBase2(); // Calcoliamo il logaritmo in base 2 arrotondato per eccesso della costante
                    
                    uint64_t NextPow2Raw = (uint64_t)1 << ShiftAmount; // Cerchiamo la potenza di 2 successiva alla costante
                    uint64_t DiffRaw = NextPow2Raw - Val.getZExtValue();

                    // Sblocchiamo lo strength reduction avanzato SOLO se la differenza è esattamente 1 oppure 0
                    if (DiffRaw == 1 || DiffRaw == 0) {
                        Constant *ShiftCountVal = ConstantInt::get(CI->getType(), ShiftAmount);

                        // Creiamo lo Shift principale e inseriamolo prima della MUL
                        Instruction *NewShl = BinaryOperator::Create(Instruction::Shl, Variable, ShiftCountVal);
                        NewShl->insertBefore(&I);

                        if (DiffRaw == 0) { // costante = potenza di 2
                            I.replaceAllUsesWith(NewShl); // Rimpiazziamo le istruzioni che usavano la MUL con lo Shift
                            Instruction *DeadInst = &I;
                            --InstIter;
                            DeadInst->eraseFromParent();
                            Modified = true;
                            break;
                        }

                        // Creiamo la sottrazione e inseriamola prima della MUL
                        Instruction *NewSub = BinaryOperator::Create(Instruction::Sub, NewShl, Variable);
                        NewSub->insertBefore(&I);

                        I.replaceAllUsesWith(NewSub); // Rimpiazzo le istruzioni che usavano la MUL con la nuova sottrazione
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                        break;
                    }
                }
            }
            
            // =========================================================================
            // CASO 2: La costante della MUL è a DESTRA (Op1)
            // =========================================================================
            if (ConstantInt *CI = dyn_cast<ConstantInt>(Op1)) {
                const APInt &Val = CI->getValue(); 
                Value *Variable = Op0; 

                if (Val == 1) {
                    // Identità algebrica
                    I.replaceAllUsesWith(Op0);
                    Instruction *DeadInst = &I;
                    --InstIter;
                    DeadInst->eraseFromParent();
                    Modified = true;
                } else if (!dyn_cast<ConstantInt>(Op0)) {
                    unsigned ShiftAmount = Val.ceilLogBase2();
                    
                    uint64_t NextPow2Raw = (uint64_t)1 << ShiftAmount;
                    uint64_t DiffRaw = NextPow2Raw - Val.getZExtValue();

                    // Sblocchiamo lo strength reduction avanzato SOLO se il divario è esattamente 1
                    if (DiffRaw == 1 || DiffRaw == 0) {
                        Constant *ShiftCountVal = ConstantInt::get(CI->getType(), ShiftAmount);

                        // Creiamo lo Shift principale
                        Instruction *NewShl = BinaryOperator::Create(Instruction::Shl, Variable, ShiftCountVal);
                        NewShl->insertBefore(&I);

                        if (DiffRaw == 0) { // costante = potenza di 2
                            I.replaceAllUsesWith(NewShl);
                            Instruction *DeadInst = &I;
                            --InstIter;
                            DeadInst->eraseFromParent();
                            Modified = true;
                            break;
                        }

                        // Creiamo la sottrazione finale
                        Instruction *NewSub = BinaryOperator::Create(Instruction::Sub, NewShl, Variable);
                        NewSub->insertBefore(&I);

                        // Cancellazione e rimpiazzo della vecchia mul
                        I.replaceAllUsesWith(NewSub);
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                        break;
                    }
                }
            }
            break;
        }
            case Instruction::Add: {
                Value *Op0 = I.getOperand(0);
                Value *Op1 = I.getOperand(1);

                // =========================================================================
                // La costante della ADD è a SINISTRA (Op0)
                // =========================================================================
                if (ConstantInt *CI = dyn_cast<ConstantInt>(Op0)) {
                    int64_t Val = CI->getSExtValue(); 
                    Value *Variable = Op1;

                    if (Val == 0) { // identita algebrica
                        // Identità algebrica (0 + b -> b)
                        I.replaceAllUsesWith(Variable);
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                    } 
                    else { // Ottimizzazione Multi-Instruction
                        
                        std::vector<Instruction*> InstsToScheduleForRemoval; //creiamo un vettore per le istruzioni da rimuovere in seguito

                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) { // controlliamo che l'utente sia un'istruzione
                                if (UserInst->getOpcode() == Instruction::Sub) { // controlliamo che l'istruzione sia una SUB
                                    // Verifichiamo che il risultato dell'addizione sia l'operando sinistro della SUB
                                    if (UserInst->getOperand(0) == &I) {
                                        if (ConstantInt *SubOp1 = dyn_cast<ConstantInt>(UserInst->getOperand(1))) { //controlliamo che l'operando destro della SUB sia una costante
                                            if (SubOp1->getSExtValue() == Val) {
                                                
                                                // Sostituiamo gli usi della SUB con la variabile originaria
                                                UserInst->replaceAllUsesWith(Variable);
                                                InstsToScheduleForRemoval.push_back(UserInst);
                                                Modified = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // Cancellazione sicura delle SUB trovate
                        for (Instruction *DeadInst : InstsToScheduleForRemoval) {
                            DeadInst->eraseFromParent();
                        }
                    }
                }

                // =========================================================================
                // La costante della ADD è a DESTRA (Op1)
                // =========================================================================
                if (ConstantInt *CI = dyn_cast<ConstantInt>(Op1)) {
                    int64_t Val = CI->getSExtValue(); 
                    Value *Variable = Op0;

                    if (Val == 0) { // identita algebrica
                        // Identità algebrica (b + 0 -> b)
                        I.replaceAllUsesWith(Variable);
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                    } 
                    else {
                        // Ottimizzazione Multi-Instruction
                        std::vector<Instruction*> InstsToScheduleForRemoval;

                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) { //controlliamo che l'utente sia un'istruzione
                                if (UserInst->getOpcode() == Instruction::Sub) {
                                    // Verifichiamo che l'addizione sia l'operando sinistro della SUB
                                    if (UserInst->getOperand(0) == &I) {
                                        if (ConstantInt *SubOp1 = dyn_cast<ConstantInt>(UserInst->getOperand(1))) {
                                            if (SubOp1->getSExtValue() == Val) {
                                                
                                                // Sostituiamo gli usi della SUB con la variabile originaria
                                                UserInst->replaceAllUsesWith(Variable);
                                                InstsToScheduleForRemoval.push_back(UserInst);
                                                Modified = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // Cancellazione sicura delle SUB trovate
                        for (Instruction *DeadInst : InstsToScheduleForRemoval) {
                            DeadInst->eraseFromParent();
                        }
                    }
                }

                break;
            }
            default:
                break;
            }
        }
    }

    if (Modified) { // Diciamo a LLVM che il pass ha modificato il codice, così da invalidare le analisi precedenti
      return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};

} // namespace

//-----------------------------------------------------------------------------
// Registrazione del Plugin per il New PM
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLocalOptsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "local-opts") {
                    FPM.addPass(LocalOpts());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLocalOptsPluginInfo();
}