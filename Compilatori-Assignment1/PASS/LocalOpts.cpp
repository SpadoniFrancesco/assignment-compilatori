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
            case Instruction::Mul: {
            Value *Op0 = I.getOperand(0);
            Value *Op1 = I.getOperand(1);

            // =========================================================================
            // CASO 1: La costante della MUL è a SINISTRA (Op0)
            // =========================================================================
            if (ConstantInt *CI = dyn_cast<ConstantInt>(Op0)) {
                const APInt &Val = CI->getValue(); 
                Value *Variable = Op1; 

                if (Val == 1) {
                    // Identità algebrica basica
                    I.replaceAllUsesWith(Op1);
                    Instruction *DeadInst = &I;
                    --InstIter;
                    DeadInst->eraseFromParent();
                    Modified = true;
                } else if (!dyn_cast<ConstantInt>(Op1)) {
                    unsigned ShiftAmount = Val.ceilLogBase2();
                    
                    uint64_t NextPow2Raw = (uint64_t)1 << ShiftAmount;
                    uint64_t DiffRaw = NextPow2Raw - Val.getZExtValue();

                    // Sblocchiamo lo strength reduction avanzato SOLO se il divario è esattamente 1
                    if (DiffRaw == 1) {
                        Constant *ShiftCountVal = ConstantInt::get(CI->getType(), ShiftAmount);

                        // Creiamo lo Shift principale
                        Instruction *NewShl = BinaryOperator::Create(Instruction::Shl, Variable, ShiftCountVal);
                        NewShl->insertBefore(&I);

                        // Creiamo la sottrazione
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
                    if (DiffRaw == 1) {
                        Constant *ShiftCountVal = ConstantInt::get(CI->getType(), ShiftAmount);

                        // Creiamo lo Shift principale
                        Instruction *NewShl = BinaryOperator::Create(Instruction::Shl, Variable, ShiftCountVal);
                        NewShl->insertBefore(&I);

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

                    if (Val == 0) {
                        // Identità algebrica (0 + b -> b)
                        I.replaceAllUsesWith(Variable);
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                    } 
                    else {
                        // Ottimizzazione Multi-Instruction (Cerca le coppie SUB inverse)
                        std::vector<Instruction*> InstsToScheduleForRemoval;

                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
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

                // =========================================================================
                // La costante della ADD è a DESTRA (Op1)
                // =========================================================================
                if (ConstantInt *CI = dyn_cast<ConstantInt>(Op1)) {
                    int64_t Val = CI->getSExtValue(); 
                    Value *Variable = Op0;

                    if (Val == 0) {
                        // Identità algebrica (b + 0 -> b)
                        I.replaceAllUsesWith(Variable);
                        Instruction *DeadInst = &I;
                        --InstIter;
                        DeadInst->eraseFromParent();
                        Modified = true;
                    } 
                    else {
                        // Ottimizzazione Multi-Instruction (Cerca le coppie SUB inverse)
                        std::vector<Instruction*> InstsToScheduleForRemoval;

                        for (User *U : I.users()) {
                            if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
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

    if (Modified) {
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