//===-- AMDGPUTargetMachine.cpp - TargetMachine for hw codegen targets-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains both AMDGPU target machine and the CodeGen pass builder.
/// The AMDGPU target machine contains all of the hardware specific information
/// needed to emit code for SI+ GPUs in the legacy pass manager pipeline. The
/// CodeGen pass builder handles the pass pipeline for new pass manager.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetMachine.h"
#include "AMDGPU.h"
#include "AMDGPUAliasAnalysis.h"
#include "AMDGPUCtorDtorLowering.h"
#include "AMDGPUExportClustering.h"
#include "AMDGPUExportKernelRuntimeHandles.h"
#include "AMDGPUIGroupLP.h"
#include "AMDGPUISelDAGToDAG.h"
#include "AMDGPUMacroFusion.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "AMDGPUPreloadKernArgProlog.h"
#include "AMDGPUPrepareAGPRAlloc.h"
#include "AMDGPURemoveIncompatibleFunctions.h"
#include "AMDGPUReserveWWMRegs.h"
#include "AMDGPUResourceUsageAnalysis.h"
#include "AMDGPUSplitModule.h"
#include "AMDGPUTargetObjectFile.h"
#include "AMDGPUTargetTransformInfo.h"
#include "AMDGPUUnifyDivergentExitNodes.h"
#include "AMDGPUWaitSGPRHazards.h"
#include "GCNDPPCombine.h"
#include "GCNIterativeScheduler.h"
#include "GCNNSAReassign.h"
#include "GCNPreRALongBranchReg.h"
#include "GCNPreRAOptimizations.h"
#include "GCNRewritePartialRegUses.h"
#include "GCNSchedStrategy.h"
#include "GCNVOPDUtils.h"
#include "R600.h"
#include "R600TargetMachine.h"
#include "SIFixSGPRCopies.h"
#include "SIFixVGPRCopies.h"
#include "SIFoldOperands.h"
#include "SIFormMemoryClauses.h"
#include "SILoadStoreOptimizer.h"
#include "SILowerControlFlow.h"
#include "SILowerSGPRSpills.h"
#include "SILowerWWMCopies.h"
#include "SIMachineFunctionInfo.h"
#include "SIMachineScheduler.h"
#include "SIOptimizeExecMasking.h"
#include "SIOptimizeExecMaskingPreRA.h"
#include "SIOptimizeVGPRLiveRange.h"
#include "SIPeepholeSDWA.h"
#include "SIPostRABundler.h"
#include "SIPreAllocateWWMRegs.h"
#include "SIShrinkInstructions.h"
#include "SIWholeQuadMode.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/KernelInfo.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/CodeGen/DeadMachineInstructionElim.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/Localizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineCSE.h"
#include "llvm/CodeGen/MachineLICM.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PostRAHazardRecognizer.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Transforms/HipStdPar/HipStdPar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/ExpandVariadics.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/FlattenCFG.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/InferAddressSpaces.h"
#include "llvm/Transforms/Scalar/LoopDataPrefetch.h"
#include "llvm/Transforms/Scalar/NaryReassociate.h"
#include "llvm/Transforms/Scalar/SeparateConstOffsetFromGEP.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Scalar/StraightLineStrengthReduce.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/SimplifyLibCalls.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"
#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include <optional>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {
class SGPRRegisterRegAlloc : public RegisterRegAllocBase<SGPRRegisterRegAlloc> {
public:
  SGPRRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

class VGPRRegisterRegAlloc : public RegisterRegAllocBase<VGPRRegisterRegAlloc> {
public:
  VGPRRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

class WWMRegisterRegAlloc : public RegisterRegAllocBase<WWMRegisterRegAlloc> {
public:
  WWMRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
      : RegisterRegAllocBase(N, D, C) {}
};

static bool onlyAllocateSGPRs(const TargetRegisterInfo &TRI,
                              const MachineRegisterInfo &MRI,
                              const Register Reg) {
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC);
}

static bool onlyAllocateVGPRs(const TargetRegisterInfo &TRI,
                              const MachineRegisterInfo &MRI,
                              const Register Reg) {
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return !static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC);
}

static bool onlyAllocateWWMRegs(const TargetRegisterInfo &TRI,
                                const MachineRegisterInfo &MRI,
                                const Register Reg) {
  const SIMachineFunctionInfo *MFI =
      MRI.getMF().getInfo<SIMachineFunctionInfo>();
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return !static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC) &&
         MFI->checkFlag(Reg, AMDGPU::VirtRegFlag::WWM_REG);
}

/// -{sgpr|wwm|vgpr}-regalloc=... command line option.
static FunctionPass *useDefaultRegisterAllocator() { return nullptr; }

/// A dummy default pass factory indicates whether the register allocator is
/// overridden on the command line.
static llvm::once_flag InitializeDefaultSGPRRegisterAllocatorFlag;
static llvm::once_flag InitializeDefaultVGPRRegisterAllocatorFlag;
static llvm::once_flag InitializeDefaultWWMRegisterAllocatorFlag;

static SGPRRegisterRegAlloc
defaultSGPRRegAlloc("default",
                    "pick SGPR register allocator based on -O option",
                    useDefaultRegisterAllocator);

static cl::opt<SGPRRegisterRegAlloc::FunctionPassCtor, false,
               RegisterPassParser<SGPRRegisterRegAlloc>>
SGPRRegAlloc("sgpr-regalloc", cl::Hidden, cl::init(&useDefaultRegisterAllocator),
             cl::desc("Register allocator to use for SGPRs"));

static cl::opt<VGPRRegisterRegAlloc::FunctionPassCtor, false,
               RegisterPassParser<VGPRRegisterRegAlloc>>
VGPRRegAlloc("vgpr-regalloc", cl::Hidden, cl::init(&useDefaultRegisterAllocator),
             cl::desc("Register allocator to use for VGPRs"));

static cl::opt<WWMRegisterRegAlloc::FunctionPassCtor, false,
               RegisterPassParser<WWMRegisterRegAlloc>>
    WWMRegAlloc("wwm-regalloc", cl::Hidden,
                cl::init(&useDefaultRegisterAllocator),
                cl::desc("Register allocator to use for WWM registers"));

static void initializeDefaultSGPRRegisterAllocatorOnce() {
  RegisterRegAlloc::FunctionPassCtor Ctor = SGPRRegisterRegAlloc::getDefault();

  if (!Ctor) {
    Ctor = SGPRRegAlloc;
    SGPRRegisterRegAlloc::setDefault(SGPRRegAlloc);
  }
}

static void initializeDefaultVGPRRegisterAllocatorOnce() {
  RegisterRegAlloc::FunctionPassCtor Ctor = VGPRRegisterRegAlloc::getDefault();

  if (!Ctor) {
    Ctor = VGPRRegAlloc;
    VGPRRegisterRegAlloc::setDefault(VGPRRegAlloc);
  }
}

static void initializeDefaultWWMRegisterAllocatorOnce() {
  RegisterRegAlloc::FunctionPassCtor Ctor = WWMRegisterRegAlloc::getDefault();

  if (!Ctor) {
    Ctor = WWMRegAlloc;
    WWMRegisterRegAlloc::setDefault(WWMRegAlloc);
  }
}

static FunctionPass *createBasicSGPRRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateSGPRs);
}

static FunctionPass *createGreedySGPRRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateSGPRs);
}

static FunctionPass *createFastSGPRRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateSGPRs, false);
}

static FunctionPass *createBasicVGPRRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateVGPRs);
}

static FunctionPass *createGreedyVGPRRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateVGPRs);
}

static FunctionPass *createFastVGPRRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateVGPRs, true);
}

static FunctionPass *createBasicWWMRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateWWMRegs);
}

static FunctionPass *createGreedyWWMRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateWWMRegs);
}

static FunctionPass *createFastWWMRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateWWMRegs, false);
}

static SGPRRegisterRegAlloc basicRegAllocSGPR(
  "basic", "basic register allocator", createBasicSGPRRegisterAllocator);
static SGPRRegisterRegAlloc greedyRegAllocSGPR(
  "greedy", "greedy register allocator", createGreedySGPRRegisterAllocator);

static SGPRRegisterRegAlloc fastRegAllocSGPR(
  "fast", "fast register allocator", createFastSGPRRegisterAllocator);


static VGPRRegisterRegAlloc basicRegAllocVGPR(
  "basic", "basic register allocator", createBasicVGPRRegisterAllocator);
static VGPRRegisterRegAlloc greedyRegAllocVGPR(
  "greedy", "greedy register allocator", createGreedyVGPRRegisterAllocator);

static VGPRRegisterRegAlloc fastRegAllocVGPR(
  "fast", "fast register allocator", createFastVGPRRegisterAllocator);
static WWMRegisterRegAlloc basicRegAllocWWMReg("basic",
                                               "basic register allocator",
                                               createBasicWWMRegisterAllocator);
static WWMRegisterRegAlloc
    greedyRegAllocWWMReg("greedy", "greedy register allocator",
                         createGreedyWWMRegisterAllocator);
static WWMRegisterRegAlloc fastRegAllocWWMReg("fast", "fast register allocator",
                                              createFastWWMRegisterAllocator);

static bool isLTOPreLink(ThinOrFullLTOPhase Phase) {
  return Phase == ThinOrFullLTOPhase::FullLTOPreLink ||
         Phase == ThinOrFullLTOPhase::ThinLTOPreLink;
}
} // anonymous namespace

static cl::opt<bool>
EnableEarlyIfConversion("amdgpu-early-ifcvt", cl::Hidden,
                        cl::desc("Run early if-conversion"),
                        cl::init(false));

static cl::opt<bool>
OptExecMaskPreRA("amdgpu-opt-exec-mask-pre-ra", cl::Hidden,
            cl::desc("Run pre-RA exec mask optimizations"),
            cl::init(true));

static cl::opt<bool>
    LowerCtorDtor("amdgpu-lower-global-ctor-dtor",
                  cl::desc("Lower GPU ctor / dtors to globals on the device."),
                  cl::init(true), cl::Hidden);

// Option to disable vectorizer for tests.
static cl::opt<bool> EnableLoadStoreVectorizer(
  "amdgpu-load-store-vectorizer",
  cl::desc("Enable load store vectorizer"),
  cl::init(true),
  cl::Hidden);

// Option to control global loads scalarization
static cl::opt<bool> ScalarizeGlobal(
  "amdgpu-scalarize-global-loads",
  cl::desc("Enable global load scalarization"),
  cl::init(true),
  cl::Hidden);

// Option to run internalize pass.
static cl::opt<bool> InternalizeSymbols(
  "amdgpu-internalize-symbols",
  cl::desc("Enable elimination of non-kernel functions and unused globals"),
  cl::init(false),
  cl::Hidden);

// Option to inline all early.
static cl::opt<bool> EarlyInlineAll(
  "amdgpu-early-inline-all",
  cl::desc("Inline all functions early"),
  cl::init(false),
  cl::Hidden);

static cl::opt<bool> RemoveIncompatibleFunctions(
    "amdgpu-enable-remove-incompatible-functions", cl::Hidden,
    cl::desc("Enable removal of functions when they"
             "use features not supported by the target GPU"),
    cl::init(true));

static cl::opt<bool> EnableSDWAPeephole(
  "amdgpu-sdwa-peephole",
  cl::desc("Enable SDWA peepholer"),
  cl::init(true));

static cl::opt<bool> EnableDPPCombine(
  "amdgpu-dpp-combine",
  cl::desc("Enable DPP combiner"),
  cl::init(true));

// Enable address space based alias analysis
static cl::opt<bool> EnableAMDGPUAliasAnalysis("enable-amdgpu-aa", cl::Hidden,
  cl::desc("Enable AMDGPU Alias Analysis"),
  cl::init(true));

// Enable lib calls simplifications
static cl::opt<bool> EnableLibCallSimplify(
  "amdgpu-simplify-libcall",
  cl::desc("Enable amdgpu library simplifications"),
  cl::init(true),
  cl::Hidden);

static cl::opt<bool> EnableLowerKernelArguments(
  "amdgpu-ir-lower-kernel-arguments",
  cl::desc("Lower kernel argument loads in IR pass"),
  cl::init(true),
  cl::Hidden);

static cl::opt<bool> EnableRegReassign(
  "amdgpu-reassign-regs",
  cl::desc("Enable register reassign optimizations on gfx10+"),
  cl::init(true),
  cl::Hidden);

static cl::opt<bool> OptVGPRLiveRange(
    "amdgpu-opt-vgpr-liverange",
    cl::desc("Enable VGPR liverange optimizations for if-else structure"),
    cl::init(true), cl::Hidden);

static cl::opt<ScanOptions> AMDGPUAtomicOptimizerStrategy(
    "amdgpu-atomic-optimizer-strategy",
    cl::desc("Select DPP or Iterative strategy for scan"),
    cl::init(ScanOptions::Iterative),
    cl::values(
        clEnumValN(ScanOptions::DPP, "DPP", "Use DPP operations for scan"),
        clEnumValN(ScanOptions::Iterative, "Iterative",
                   "Use Iterative approach for scan"),
        clEnumValN(ScanOptions::None, "None", "Disable atomic optimizer")));

// Enable Mode register optimization
static cl::opt<bool> EnableSIModeRegisterPass(
  "amdgpu-mode-register",
  cl::desc("Enable mode register pass"),
  cl::init(true),
  cl::Hidden);

// Enable GFX11+ s_delay_alu insertion
static cl::opt<bool>
    EnableInsertDelayAlu("amdgpu-enable-delay-alu",
                         cl::desc("Enable s_delay_alu insertion"),
                         cl::init(true), cl::Hidden);

// Enable GFX11+ VOPD
static cl::opt<bool>
    EnableVOPD("amdgpu-enable-vopd",
               cl::desc("Enable VOPD, dual issue of VALU in wave32"),
               cl::init(true), cl::Hidden);

// Option is used in lit tests to prevent deadcoding of patterns inspected.
static cl::opt<bool>
EnableDCEInRA("amdgpu-dce-in-ra",
    cl::init(true), cl::Hidden,
    cl::desc("Enable machine DCE inside regalloc"));

static cl::opt<bool> EnableSetWavePriority("amdgpu-set-wave-priority",
                                           cl::desc("Adjust wave priority"),
                                           cl::init(false), cl::Hidden);

static cl::opt<bool> EnableScalarIRPasses(
  "amdgpu-scalar-ir-passes",
  cl::desc("Enable scalar IR passes"),
  cl::init(true),
  cl::Hidden);

static cl::opt<bool>
    EnableSwLowerLDS("amdgpu-enable-sw-lower-lds",
                     cl::desc("Enable lowering of lds to global memory pass "
                              "and asan instrument resulting IR."),
                     cl::init(true), cl::Hidden);

static cl::opt<bool, true> EnableLowerModuleLDS(
    "amdgpu-enable-lower-module-lds", cl::desc("Enable lower module lds pass"),
    cl::location(AMDGPUTargetMachine::EnableLowerModuleLDS), cl::init(true),
    cl::Hidden);

static cl::opt<bool> EnablePreRAOptimizations(
    "amdgpu-enable-pre-ra-optimizations",
    cl::desc("Enable Pre-RA optimizations pass"), cl::init(true),
    cl::Hidden);

static cl::opt<bool> EnablePromoteKernelArguments(
    "amdgpu-enable-promote-kernel-arguments",
    cl::desc("Enable promotion of flat kernel pointer arguments to global"),
    cl::Hidden, cl::init(true));

static cl::opt<bool> EnableImageIntrinsicOptimizer(
    "amdgpu-enable-image-intrinsic-optimizer",
    cl::desc("Enable image intrinsic optimizer pass"), cl::init(true),
    cl::Hidden);

static cl::opt<bool>
    EnableLoopPrefetch("amdgpu-loop-prefetch",
                       cl::desc("Enable loop data prefetch on AMDGPU"),
                       cl::Hidden, cl::init(false));

static cl::opt<std::string>
    AMDGPUSchedStrategy("amdgpu-sched-strategy",
                        cl::desc("Select custom AMDGPU scheduling strategy."),
                        cl::Hidden, cl::init(""));

static cl::opt<bool> EnableRewritePartialRegUses(
    "amdgpu-enable-rewrite-partial-reg-uses",
    cl::desc("Enable rewrite partial reg uses pass"), cl::init(true),
    cl::Hidden);

static cl::opt<bool> EnableHipStdPar(
  "amdgpu-enable-hipstdpar",
  cl::desc("Enable HIP Standard Parallelism Offload support"), cl::init(false),
  cl::Hidden);

static cl::opt<bool>
    EnableAMDGPUAttributor("amdgpu-attributor-enable",
                           cl::desc("Enable AMDGPUAttributorPass"),
                           cl::init(true), cl::Hidden);

static cl::opt<bool> NewRegBankSelect(
    "new-reg-bank-select",
    cl::desc("Run amdgpu-regbankselect and amdgpu-regbanklegalize instead of "
             "regbankselect"),
    cl::init(false), cl::Hidden);

static cl::opt<bool> HasClosedWorldAssumption(
    "amdgpu-link-time-closed-world",
    cl::desc("Whether has closed-world assumption at link time"),
    cl::init(false), cl::Hidden);

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUTarget() {
  // Register the target
  RegisterTargetMachine<R600TargetMachine> X(getTheR600Target());
  RegisterTargetMachine<GCNTargetMachine> Y(getTheGCNTarget());

  PassRegistry *PR = PassRegistry::getPassRegistry();
  initializeR600ClauseMergePassPass(*PR);
  initializeR600ControlFlowFinalizerPass(*PR);
  initializeR600PacketizerPass(*PR);
  initializeR600ExpandSpecialInstrsPassPass(*PR);
  initializeR600VectorRegMergerPass(*PR);
  initializeR600EmitClauseMarkersPass(*PR);
  initializeR600MachineCFGStructurizerPass(*PR);
  initializeGlobalISel(*PR);
  initializeAMDGPUAsmPrinterPass(*PR);
  initializeAMDGPUDAGToDAGISelLegacyPass(*PR);
  initializeAMDGPUPrepareAGPRAllocLegacyPass(*PR);
  initializeGCNDPPCombineLegacyPass(*PR);
  initializeSILowerI1CopiesLegacyPass(*PR);
  initializeAMDGPUGlobalISelDivergenceLoweringPass(*PR);
  initializeAMDGPURegBankSelectPass(*PR);
  initializeAMDGPURegBankLegalizePass(*PR);
  initializeSILowerWWMCopiesLegacyPass(*PR);
  initializeAMDGPUMarkLastScratchLoadLegacyPass(*PR);
  initializeSILowerSGPRSpillsLegacyPass(*PR);
  initializeSIFixSGPRCopiesLegacyPass(*PR);
  initializeSIFixVGPRCopiesLegacyPass(*PR);
  initializeSIFoldOperandsLegacyPass(*PR);
  initializeSIPeepholeSDWALegacyPass(*PR);
  initializeSIShrinkInstructionsLegacyPass(*PR);
  initializeSIOptimizeExecMaskingPreRALegacyPass(*PR);
  initializeSIOptimizeVGPRLiveRangeLegacyPass(*PR);
  initializeSILoadStoreOptimizerLegacyPass(*PR);
  initializeAMDGPUCtorDtorLoweringLegacyPass(*PR);
  initializeAMDGPUAlwaysInlinePass(*PR);
  initializeAMDGPUSwLowerLDSLegacyPass(*PR);
  initializeAMDGPUAnnotateUniformValuesLegacyPass(*PR);
  initializeAMDGPUArgumentUsageInfoPass(*PR);
  initializeAMDGPUAtomicOptimizerPass(*PR);
  initializeAMDGPULowerKernelArgumentsPass(*PR);
  initializeAMDGPUPromoteKernelArgumentsPass(*PR);
  initializeAMDGPULowerKernelAttributesPass(*PR);
  initializeAMDGPUExportKernelRuntimeHandlesLegacyPass(*PR);
  initializeAMDGPUPostLegalizerCombinerPass(*PR);
  initializeAMDGPUPreLegalizerCombinerPass(*PR);
  initializeAMDGPURegBankCombinerPass(*PR);
  initializeAMDGPUPromoteAllocaPass(*PR);
  initializeAMDGPUCodeGenPreparePass(*PR);
  initializeAMDGPULateCodeGenPrepareLegacyPass(*PR);
  initializeAMDGPURemoveIncompatibleFunctionsLegacyPass(*PR);
  initializeAMDGPULowerModuleLDSLegacyPass(*PR);
  initializeAMDGPULowerBufferFatPointersPass(*PR);
  initializeAMDGPUReserveWWMRegsLegacyPass(*PR);
  initializeAMDGPURewriteAGPRCopyMFMALegacyPass(*PR);
  initializeAMDGPURewriteOutArgumentsPass(*PR);
  initializeAMDGPURewriteUndefForPHILegacyPass(*PR);
  initializeSIAnnotateControlFlowLegacyPass(*PR);
  initializeAMDGPUInsertDelayAluLegacyPass(*PR);
  initializeSIInsertHardClausesLegacyPass(*PR);
  initializeSIInsertWaitcntsLegacyPass(*PR);
  initializeSIModeRegisterLegacyPass(*PR);
  initializeSIWholeQuadModeLegacyPass(*PR);
  initializeSILowerControlFlowLegacyPass(*PR);
  initializeSIPreEmitPeepholeLegacyPass(*PR);
  initializeSILateBranchLoweringLegacyPass(*PR);
  initializeSIMemoryLegalizerLegacyPass(*PR);
  initializeSIOptimizeExecMaskingLegacyPass(*PR);
  initializeSIPreAllocateWWMRegsLegacyPass(*PR);
  initializeSIFormMemoryClausesLegacyPass(*PR);
  initializeSIPostRABundlerLegacyPass(*PR);
  initializeGCNCreateVOPDLegacyPass(*PR);
  initializeAMDGPUUnifyDivergentExitNodesPass(*PR);
  initializeAMDGPUAAWrapperPassPass(*PR);
  initializeAMDGPUExternalAAWrapperPass(*PR);
  initializeAMDGPUImageIntrinsicOptimizerPass(*PR);
  initializeAMDGPUPrintfRuntimeBindingPass(*PR);
  initializeAMDGPUResourceUsageAnalysisWrapperPassPass(*PR);
  initializeGCNNSAReassignLegacyPass(*PR);
  initializeGCNPreRAOptimizationsLegacyPass(*PR);
  initializeGCNPreRALongBranchRegLegacyPass(*PR);
  initializeGCNRewritePartialRegUsesLegacyPass(*PR);
  initializeGCNRegPressurePrinterPass(*PR);
  initializeAMDGPUPreloadKernArgPrologLegacyPass(*PR);
  initializeAMDGPUWaitSGPRHazardsLegacyPass(*PR);
  initializeAMDGPUPreloadKernelArgumentsLegacyPass(*PR);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  return std::make_unique<AMDGPUTargetObjectFile>();
}

static ScheduleDAGInstrs *createSIMachineScheduler(MachineSchedContext *C) {
  return new SIScheduleDAGMI(C);
}

static ScheduleDAGInstrs *
createGCNMaxOccupancyMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG =
    new GCNScheduleDAGMILive(C, std::make_unique<GCNMaxOccupancySchedStrategy>(C));
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  DAG->addMutation(createAMDGPUMacroFusionDAGMutation());
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  return DAG;
}

static ScheduleDAGInstrs *
createGCNMaxILPMachineScheduler(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG =
      new GCNScheduleDAGMILive(C, std::make_unique<GCNMaxILPSchedStrategy>(C));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *
createGCNMaxMemoryClauseMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG = new GCNScheduleDAGMILive(
      C, std::make_unique<GCNMaxMemoryClauseSchedStrategy>(C));
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  return DAG;
}

static ScheduleDAGInstrs *
createIterativeGCNMaxOccupancyMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  auto *DAG = new GCNIterativeScheduler(
      C, GCNIterativeScheduler::SCHEDULE_LEGACYMAXOCCUPANCY);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *createMinRegScheduler(MachineSchedContext *C) {
  auto *DAG = new GCNIterativeScheduler(
      C, GCNIterativeScheduler::SCHEDULE_MINREGFORCED);
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *
createIterativeILPMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  auto *DAG = new GCNIterativeScheduler(C, GCNIterativeScheduler::SCHEDULE_ILP);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createAMDGPUMacroFusionDAGMutation());
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static MachineSchedRegistry
SISchedRegistry("si", "Run SI's custom scheduler",
                createSIMachineScheduler);

static MachineSchedRegistry
GCNMaxOccupancySchedRegistry("gcn-max-occupancy",
                             "Run GCN scheduler to maximize occupancy",
                             createGCNMaxOccupancyMachineScheduler);

static MachineSchedRegistry
    GCNMaxILPSchedRegistry("gcn-max-ilp", "Run GCN scheduler to maximize ilp",
                           createGCNMaxILPMachineScheduler);

static MachineSchedRegistry GCNMaxMemoryClauseSchedRegistry(
    "gcn-max-memory-clause", "Run GCN scheduler to maximize memory clause",
    createGCNMaxMemoryClauseMachineScheduler);

static MachineSchedRegistry IterativeGCNMaxOccupancySchedRegistry(
    "gcn-iterative-max-occupancy-experimental",
    "Run GCN scheduler to maximize occupancy (experimental)",
    createIterativeGCNMaxOccupancyMachineScheduler);

static MachineSchedRegistry GCNMinRegSchedRegistry(
    "gcn-iterative-minreg",
    "Run GCN iterative scheduler for minimal register usage (experimental)",
    createMinRegScheduler);

static MachineSchedRegistry GCNILPSchedRegistry(
    "gcn-iterative-ilp",
    "Run GCN iterative scheduler for ILP scheduling (experimental)",
    createIterativeILPMachineScheduler);

static StringRef computeDataLayout(const Triple &TT) {
  if (TT.getArch() == Triple::r600) {
    // 32-bit pointers.
    return "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128"
           "-v192:256-v256:256-v512:512-v1024:1024-v2048:2048-n32:64-S32-A5-G1";
  }

  // 32-bit private, local, and region pointers. 64-bit global, constant and
  // flat. 160-bit non-integral fat buffer pointers that include a 128-bit
  // buffer descriptor and a 32-bit offset, which are indexed by 32-bit values
  // (address space 7), and 128-bit non-integral buffer resourcees (address
  // space 8) which cannot be non-trivilally accessed by LLVM memory operations
  // like getelementptr.
  return "e-p:64:64-p1:64:64-p2:32:32-p3:32:32-p4:64:64-p5:32:32-p6:32:32"
         "-p7:160:256:256:32-p8:128:128:128:48-p9:192:256:256:32-i64:64-"
         "v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-"
         "v1024:1024-v2048:2048-n32:64-S32-A5-G1-ni:7:8:9";
}

LLVM_READNONE
static StringRef getGPUOrDefault(const Triple &TT, StringRef GPU) {
  if (!GPU.empty())
    return GPU;

  // Need to default to a target with flat support for HSA.
  if (TT.isAMDGCN())
    return TT.getOS() == Triple::AMDHSA ? "generic-hsa" : "generic";

  return "r600";
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // The AMDGPU toolchain only supports generating shared objects, so we
  // must always use PIC.
  return Reloc::PIC_;
}

AMDGPUTargetMachine::AMDGPUTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OptLevel)
    : CodeGenTargetMachineImpl(
          T, computeDataLayout(TT), TT, getGPUOrDefault(TT, CPU), FS, Options,
          getEffectiveRelocModel(RM),
          getEffectiveCodeModel(CM, CodeModel::Small), OptLevel),
      TLOF(createTLOF(getTargetTriple())) {
  initAsmInfo();
  if (TT.isAMDGCN()) {
    if (getMCSubtargetInfo()->checkFeatures("+wavefrontsize64"))
      MRI.reset(llvm::createGCNMCRegisterInfo(AMDGPUDwarfFlavour::Wave64));
    else if (getMCSubtargetInfo()->checkFeatures("+wavefrontsize32"))
      MRI.reset(llvm::createGCNMCRegisterInfo(AMDGPUDwarfFlavour::Wave32));
  }
}

bool AMDGPUTargetMachine::EnableFunctionCalls = false;
bool AMDGPUTargetMachine::EnableLowerModuleLDS = true;

AMDGPUTargetMachine::~AMDGPUTargetMachine() = default;

StringRef AMDGPUTargetMachine::getGPUName(const Function &F) const {
  Attribute GPUAttr = F.getFnAttribute("target-cpu");
  return GPUAttr.isValid() ? GPUAttr.getValueAsString() : getTargetCPU();
}

StringRef AMDGPUTargetMachine::getFeatureString(const Function &F) const {
  Attribute FSAttr = F.getFnAttribute("target-features");

  return FSAttr.isValid() ? FSAttr.getValueAsString()
                          : getTargetFeatureString();
}

llvm::ScheduleDAGInstrs *
AMDGPUTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG = createSchedLive(C);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

/// Predicate for Internalize pass.
static bool mustPreserveGV(const GlobalValue &GV) {
  if (const Function *F = dyn_cast<Function>(&GV))
    return F->isDeclaration() || F->getName().starts_with("__asan_") ||
           F->getName().starts_with("__sanitizer_") ||
           AMDGPU::isEntryFunctionCC(F->getCallingConv());

  GV.removeDeadConstantUsers();
  return !GV.use_empty();
}

void AMDGPUTargetMachine::registerDefaultAliasAnalyses(AAManager &AAM) {
  AAM.registerFunctionAnalysis<AMDGPUAA>();
}

static Expected<ScanOptions>
parseAMDGPUAtomicOptimizerStrategy(StringRef Params) {
  if (Params.empty())
    return ScanOptions::Iterative;
  Params.consume_front("strategy=");
  auto Result = StringSwitch<std::optional<ScanOptions>>(Params)
                    .Case("dpp", ScanOptions::DPP)
                    .Cases("iterative", "", ScanOptions::Iterative)
                    .Case("none", ScanOptions::None)
                    .Default(std::nullopt);
  if (Result)
    return *Result;
  return make_error<StringError>("invalid parameter", inconvertibleErrorCode());
}

Expected<AMDGPUAttributorOptions>
parseAMDGPUAttributorPassOptions(StringRef Params) {
  AMDGPUAttributorOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');
    if (ParamName == "closed-world") {
      Result.IsClosedWorld = true;
    } else {
      return make_error<StringError>(
          formatv("invalid AMDGPUAttributor pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

void AMDGPUTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {

#define GET_PASS_REGISTRY "AMDGPUPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"

  PB.registerScalarOptimizerLateEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(InferAddressSpacesPass());
      });

  PB.registerVectorizerEndEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(InferAddressSpacesPass());
      });

  PB.registerPipelineEarlySimplificationEPCallback(
      [](ModulePassManager &PM, OptimizationLevel Level,
         ThinOrFullLTOPhase Phase) {
        if (!isLTOPreLink(Phase)) {
          // When we are not using -fgpu-rdc, we can run accelerator code
          // selection relatively early, but still after linking to prevent
          // eager removal of potentially reachable symbols.
          if (EnableHipStdPar) {
            PM.addPass(HipStdParMathFixupPass());
            PM.addPass(HipStdParAcceleratorCodeSelectionPass());
          }
          PM.addPass(AMDGPUPrintfRuntimeBindingPass());
        }

        if (Level == OptimizationLevel::O0)
          return;

        PM.addPass(AMDGPUUnifyMetadataPass());

        // We don't want to run internalization at per-module stage.
        if (InternalizeSymbols && !isLTOPreLink(Phase)) {
          PM.addPass(InternalizePass(mustPreserveGV));
          PM.addPass(GlobalDCEPass());
        }

        if (EarlyInlineAll && !EnableFunctionCalls)
          PM.addPass(AMDGPUAlwaysInlinePass());
      });

  PB.registerPeepholeEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(AMDGPUUseNativeCallsPass());
        if (EnableLibCallSimplify)
          FPM.addPass(AMDGPUSimplifyLibCallsPass());
      });

  PB.registerCGSCCOptimizerLateEPCallback(
      [this](CGSCCPassManager &PM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FunctionPassManager FPM;

        // Add promote kernel arguments pass to the opt pipeline right before
        // infer address spaces which is needed to do actual address space
        // rewriting.
        if (Level.getSpeedupLevel() > OptimizationLevel::O1.getSpeedupLevel() &&
            EnablePromoteKernelArguments)
          FPM.addPass(AMDGPUPromoteKernelArgumentsPass());

        // Add infer address spaces pass to the opt pipeline after inlining
        // but before SROA to increase SROA opportunities.
        FPM.addPass(InferAddressSpacesPass());

        // This should run after inlining to have any chance of doing
        // anything, and before other cleanup optimizations.
        FPM.addPass(AMDGPULowerKernelAttributesPass());

        if (Level != OptimizationLevel::O0) {
          // Promote alloca to vector before SROA and loop unroll. If we
          // manage to eliminate allocas before unroll we may choose to unroll
          // less.
          FPM.addPass(AMDGPUPromoteAllocaToVectorPass(*this));
        }

        PM.addPass(createCGSCCToFunctionPassAdaptor(std::move(FPM)));
      });

  // FIXME: Why is AMDGPUAttributor not in CGSCC?
  PB.registerOptimizerLastEPCallback([this](ModulePassManager &MPM,
                                            OptimizationLevel Level,
                                            ThinOrFullLTOPhase Phase) {
    if (Level != OptimizationLevel::O0) {
      if (!isLTOPreLink(Phase)) {
        AMDGPUAttributorOptions Opts;
        MPM.addPass(AMDGPUAttributorPass(*this, Opts, Phase));
      }
    }
  });

  PB.registerFullLinkTimeOptimizationLastEPCallback(
      [this](ModulePassManager &PM, OptimizationLevel Level) {
        // When we are using -fgpu-rdc, we can only run accelerator code
        // selection after linking to prevent, otherwise we end up removing
        // potentially reachable symbols that were exported as external in other
        // modules.
        if (EnableHipStdPar) {
          PM.addPass(HipStdParMathFixupPass());
          PM.addPass(HipStdParAcceleratorCodeSelectionPass());
        }
        // We want to support the -lto-partitions=N option as "best effort".
        // For that, we need to lower LDS earlier in the pipeline before the
        // module is partitioned for codegen.
        if (EnableSwLowerLDS)
          PM.addPass(AMDGPUSwLowerLDSPass(*this));
        if (EnableLowerModuleLDS)
          PM.addPass(AMDGPULowerModuleLDSPass(*this));
        if (Level != OptimizationLevel::O0) {
          // We only want to run this with O2 or higher since inliner and SROA
          // don't run in O1.
          if (Level != OptimizationLevel::O1) {
            PM.addPass(
                createModuleToFunctionPassAdaptor(InferAddressSpacesPass()));
          }
          // Do we really need internalization in LTO?
          if (InternalizeSymbols) {
            PM.addPass(InternalizePass(mustPreserveGV));
            PM.addPass(GlobalDCEPass());
          }
          if (EnableAMDGPUAttributor) {
            AMDGPUAttributorOptions Opt;
            if (HasClosedWorldAssumption)
              Opt.IsClosedWorld = true;
            PM.addPass(AMDGPUAttributorPass(
                *this, Opt, ThinOrFullLTOPhase::FullLTOPostLink));
          }
        }
        if (!NoKernelInfoEndLTO) {
          FunctionPassManager FPM;
          FPM.addPass(KernelInfoPrinter(this));
          PM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
        }
      });

  PB.registerRegClassFilterParsingCallback(
      [](StringRef FilterName) -> RegAllocFilterFunc {
        if (FilterName == "sgpr")
          return onlyAllocateSGPRs;
        if (FilterName == "vgpr")
          return onlyAllocateVGPRs;
        if (FilterName == "wwm")
          return onlyAllocateWWMRegs;
        return nullptr;
      });
}

int64_t AMDGPUTargetMachine::getNullPointerValue(unsigned AddrSpace) {
  return (AddrSpace == AMDGPUAS::LOCAL_ADDRESS ||
          AddrSpace == AMDGPUAS::PRIVATE_ADDRESS ||
          AddrSpace == AMDGPUAS::REGION_ADDRESS)
             ? -1
             : 0;
}

bool AMDGPUTargetMachine::isNoopAddrSpaceCast(unsigned SrcAS,
                                              unsigned DestAS) const {
  return AMDGPU::isFlatGlobalAddrSpace(SrcAS) &&
         AMDGPU::isFlatGlobalAddrSpace(DestAS);
}

unsigned AMDGPUTargetMachine::getAssumedAddrSpace(const Value *V) const {
  if (auto *Arg = dyn_cast<Argument>(V);
      Arg &&
      AMDGPU::isModuleEntryFunctionCC(Arg->getParent()->getCallingConv()) &&
      !Arg->hasByRefAttr())
    return AMDGPUAS::GLOBAL_ADDRESS;

  const auto *LD = dyn_cast<LoadInst>(V);
  if (!LD) // TODO: Handle invariant load like constant.
    return AMDGPUAS::UNKNOWN_ADDRESS_SPACE;

  // It must be a generic pointer loaded.
  assert(V->getType()->getPointerAddressSpace() == AMDGPUAS::FLAT_ADDRESS);

  const auto *Ptr = LD->getPointerOperand();
  if (Ptr->getType()->getPointerAddressSpace() != AMDGPUAS::CONSTANT_ADDRESS)
    return AMDGPUAS::UNKNOWN_ADDRESS_SPACE;
  // For a generic pointer loaded from the constant memory, it could be assumed
  // as a global pointer since the constant memory is only populated on the
  // host side. As implied by the offload programming model, only global
  // pointers could be referenced on the host side.
  return AMDGPUAS::GLOBAL_ADDRESS;
}

std::pair<const Value *, unsigned>
AMDGPUTargetMachine::getPredicatedAddrSpace(const Value *V) const {
  if (auto *II = dyn_cast<IntrinsicInst>(V)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::amdgcn_is_shared:
      return std::pair(II->getArgOperand(0), AMDGPUAS::LOCAL_ADDRESS);
    case Intrinsic::amdgcn_is_private:
      return std::pair(II->getArgOperand(0), AMDGPUAS::PRIVATE_ADDRESS);
    default:
      break;
    }
    return std::pair(nullptr, -1);
  }
  // Check the global pointer predication based on
  // (!is_share(p) && !is_private(p)). Note that logic 'and' is commutative and
  // the order of 'is_shared' and 'is_private' is not significant.
  Value *Ptr;
  if (match(
          const_cast<Value *>(V),
          m_c_And(m_Not(m_Intrinsic<Intrinsic::amdgcn_is_shared>(m_Value(Ptr))),
                  m_Not(m_Intrinsic<Intrinsic::amdgcn_is_private>(
                      m_Deferred(Ptr))))))
    return std::pair(Ptr, AMDGPUAS::GLOBAL_ADDRESS);

  return std::pair(nullptr, -1);
}

unsigned
AMDGPUTargetMachine::getAddressSpaceForPseudoSourceKind(unsigned Kind) const {
  switch (Kind) {
  case PseudoSourceValue::Stack:
  case PseudoSourceValue::FixedStack:
    return AMDGPUAS::PRIVATE_ADDRESS;
  case PseudoSourceValue::ConstantPool:
  case PseudoSourceValue::GOT:
  case PseudoSourceValue::JumpTable:
  case PseudoSourceValue::GlobalValueCallEntry:
  case PseudoSourceValue::ExternalSymbolCallEntry:
    return AMDGPUAS::CONSTANT_ADDRESS;
  }
  return AMDGPUAS::FLAT_ADDRESS;
}

bool AMDGPUTargetMachine::splitModule(
    Module &M, unsigned NumParts,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback) {
  // FIXME(?): Would be better to use an already existing Analysis/PassManager,
  // but all current users of this API don't have one ready and would need to
  // create one anyway. Let's hide the boilerplate for now to keep it simple.

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB(this);
  PB.registerModuleAnalyses(MAM);
  PB.registerFunctionAnalyses(FAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  MPM.addPass(AMDGPUSplitModulePass(NumParts, ModuleCallback));
  MPM.run(M, MAM);
  return true;
}

//===----------------------------------------------------------------------===//
// GCN Target Machine (SI+)
//===----------------------------------------------------------------------===//

GCNTargetMachine::GCNTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : AMDGPUTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL) {}

const TargetSubtargetInfo *
GCNTargetMachine::getSubtargetImpl(const Function &F) const {
  StringRef GPU = getGPUName(F);
  StringRef FS = getFeatureString(F);

  SmallString<128> SubtargetKey(GPU);
  SubtargetKey.append(FS);

  auto &I = SubtargetMap[SubtargetKey];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<GCNSubtarget>(TargetTriple, GPU, FS, *this);
  }

  I->setScalarizeGlobalBehavior(ScalarizeGlobal);

  return I.get();
}

TargetTransformInfo
GCNTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<GCNTTIImpl>(this, F));
}

Error GCNTargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC) {
  AMDGPUCodeGenPassBuilder CGPB(*this, Opts, PIC);
  return CGPB.buildPipeline(MPM, Out, DwoOut, FileType);
}

ScheduleDAGInstrs *
GCNTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  if (ST.enableSIScheduler())
    return createSIMachineScheduler(C);

  Attribute SchedStrategyAttr =
      C->MF->getFunction().getFnAttribute("amdgpu-sched-strategy");
  StringRef SchedStrategy = SchedStrategyAttr.isValid()
                                ? SchedStrategyAttr.getValueAsString()
                                : AMDGPUSchedStrategy;

  if (SchedStrategy == "max-ilp")
    return createGCNMaxILPMachineScheduler(C);

  if (SchedStrategy == "max-memory-clause")
    return createGCNMaxMemoryClauseMachineScheduler(C);

  if (SchedStrategy == "iterative-ilp")
    return createIterativeILPMachineScheduler(C);

  if (SchedStrategy == "iterative-minreg")
    return createMinRegScheduler(C);

  if (SchedStrategy == "iterative-maxocc")
    return createIterativeGCNMaxOccupancyMachineScheduler(C);

  return createGCNMaxOccupancyMachineScheduler(C);
}

ScheduleDAGInstrs *
GCNTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  ScheduleDAGMI *DAG =
      new GCNPostScheduleDAGMILive(C, std::make_unique<PostGenericScheduler>(C),
                                   /*RemoveKillFlags=*/true);
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::PostRA));
  if ((EnableVOPD.getNumOccurrences() ||
       getOptLevel() >= CodeGenOptLevel::Less) &&
      EnableVOPD)
    DAG->addMutation(createVOPDPairingMutation());
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  return DAG;
}
//===----------------------------------------------------------------------===//
// AMDGPU Legacy Pass Setup
//===----------------------------------------------------------------------===//

std::unique_ptr<CSEConfigBase> llvm::AMDGPUPassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

namespace {

class GCNPassConfig final : public AMDGPUPassConfig {
public:
  GCNPassConfig(TargetMachine &TM, PassManagerBase &PM)
      : AMDGPUPassConfig(TM, PM) {
    // It is necessary to know the register usage of the entire call graph.  We
    // allow calls without EnableAMDGPUFunctionCalls if they are marked
    // noinline, so this is always required.
    setRequiresCodeGenSCCOrder(true);
    substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
  }

  GCNTargetMachine &getGCNTargetMachine() const {
    return getTM<GCNTargetMachine>();
  }

  bool addPreISel() override;
  void addMachineSSAOptimization() override;
  bool addILPOpts() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  void addPreGlobalInstructionSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreRegAlloc() override;
  void addFastRegAlloc() override;
  void addOptimizedRegAlloc() override;

  FunctionPass *createSGPRAllocPass(bool Optimized);
  FunctionPass *createVGPRAllocPass(bool Optimized);
  FunctionPass *createWWMRegAllocPass(bool Optimized);
  FunctionPass *createRegAllocPass(bool Optimized) override;

  bool addRegAssignAndRewriteFast() override;
  bool addRegAssignAndRewriteOptimized() override;

  bool addPreRewrite() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
  void addPostBBSections() override;
};

} // end anonymous namespace

AMDGPUPassConfig::AMDGPUPassConfig(TargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {
  // Exceptions and StackMaps are not supported, so these passes will never do
  // anything.
  disablePass(&StackMapLivenessID);
  disablePass(&FuncletLayoutID);
  // Garbage collection is not supported.
  disablePass(&GCLoweringID);
  disablePass(&ShadowStackGCLoweringID);
}

void AMDGPUPassConfig::addEarlyCSEOrGVNPass() {
  if (getOptLevel() == CodeGenOptLevel::Aggressive)
    addPass(createGVNPass());
  else
    addPass(createEarlyCSEPass());
}

void AMDGPUPassConfig::addStraightLineScalarOptimizationPasses() {
  if (isPassEnabled(EnableLoopPrefetch, CodeGenOptLevel::Aggressive))
    addPass(createLoopDataPrefetchPass());
  addPass(createSeparateConstOffsetFromGEPPass());
  // ReassociateGEPs exposes more opportunities for SLSR. See
  // the example in reassociate-geps-and-slsr.ll.
  addPass(createStraightLineStrengthReducePass());
  // SeparateConstOffsetFromGEP and SLSR creates common expressions which GVN or
  // EarlyCSE can reuse.
  addEarlyCSEOrGVNPass();
  // Run NaryReassociate after EarlyCSE/GVN to be more effective.
  addPass(createNaryReassociatePass());
  // NaryReassociate on GEPs creates redundant common expressions, so run
  // EarlyCSE after it.
  addPass(createEarlyCSEPass());
}

void AMDGPUPassConfig::addIRPasses() {
  const AMDGPUTargetMachine &TM = getAMDGPUTargetMachine();

  if (RemoveIncompatibleFunctions && TM.getTargetTriple().isAMDGCN())
    addPass(createAMDGPURemoveIncompatibleFunctionsPass(&TM));

  // There is no reason to run these.
  disablePass(&StackMapLivenessID);
  disablePass(&FuncletLayoutID);
  disablePass(&PatchableFunctionID);

  addPass(createAMDGPUPrintfRuntimeBinding());
  if (LowerCtorDtor)
    addPass(createAMDGPUCtorDtorLoweringLegacyPass());

  if (isPassEnabled(EnableImageIntrinsicOptimizer))
    addPass(createAMDGPUImageIntrinsicOptimizerPass(&TM));

  // This can be disabled by passing ::Disable here or on the command line
  // with --expand-variadics-override=disable.
  addPass(createExpandVariadicsPass(ExpandVariadicsMode::Lowering));

  // Function calls are not supported, so make sure we inline everything.
  addPass(createAMDGPUAlwaysInlinePass());
  addPass(createAlwaysInlinerLegacyPass());

  // Handle uses of OpenCL image2d_t, image3d_t and sampler_t arguments.
  if (TM.getTargetTriple().getArch() == Triple::r600)
    addPass(createR600OpenCLImageTypeLoweringPass());

  // Make enqueued block runtime handles externally visible.
  addPass(createAMDGPUExportKernelRuntimeHandlesLegacyPass());

  // Lower LDS accesses to global memory pass if address sanitizer is enabled.
  if (EnableSwLowerLDS)
    addPass(createAMDGPUSwLowerLDSLegacyPass(&TM));

  // Runs before PromoteAlloca so the latter can account for function uses
  if (EnableLowerModuleLDS) {
    addPass(createAMDGPULowerModuleLDSLegacyPass(&TM));
  }

  // Run atomic optimizer before Atomic Expand
  if ((TM.getTargetTriple().isAMDGCN()) &&
      (TM.getOptLevel() >= CodeGenOptLevel::Less) &&
      (AMDGPUAtomicOptimizerStrategy != ScanOptions::None)) {
    addPass(createAMDGPUAtomicOptimizerPass(AMDGPUAtomicOptimizerStrategy));
  }

  addPass(createAtomicExpandLegacyPass());

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addPass(createAMDGPUPromoteAlloca());

    if (isPassEnabled(EnableScalarIRPasses))
      addStraightLineScalarOptimizationPasses();

    if (EnableAMDGPUAliasAnalysis) {
      addPass(createAMDGPUAAWrapperPass());
      addPass(createExternalAAWrapperPass([](Pass &P, Function &,
                                             AAResults &AAR) {
        if (auto *WrapperPass = P.getAnalysisIfAvailable<AMDGPUAAWrapperPass>())
          AAR.addAAResult(WrapperPass->getResult());
        }));
    }

    if (TM.getTargetTriple().isAMDGCN()) {
      // TODO: May want to move later or split into an early and late one.
      addPass(createAMDGPUCodeGenPreparePass());
    }

    // Try to hoist loop invariant parts of divisions AMDGPUCodeGenPrepare may
    // have expanded.
    if (TM.getOptLevel() > CodeGenOptLevel::Less)
      addPass(createLICMPass());
  }

  TargetPassConfig::addIRPasses();

  // EarlyCSE is not always strong enough to clean up what LSR produces. For
  // example, GVN can combine
  //
  //   %0 = add %a, %b
  //   %1 = add %b, %a
  //
  // and
  //
  //   %0 = shl nsw %a, 2
  //   %1 = shl %a, 2
  //
  // but EarlyCSE can do neither of them.
  if (isPassEnabled(EnableScalarIRPasses))
    addEarlyCSEOrGVNPass();
}

void AMDGPUPassConfig::addCodeGenPrepare() {
  if (TM->getTargetTriple().isAMDGCN() &&
      TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createAMDGPUPreloadKernelArgumentsLegacyPass(TM));

  if (TM->getTargetTriple().isAMDGCN() && EnableLowerKernelArguments)
    addPass(createAMDGPULowerKernelArgumentsPass());

  if (TM->getTargetTriple().isAMDGCN()) {
    // This lowering has been placed after codegenprepare to take advantage of
    // address mode matching (which is why it isn't put with the LDS lowerings).
    // It could be placed anywhere before uniformity annotations (an analysis
    // that it changes by splitting up fat pointers into their components)
    // but has been put before switch lowering and CFG flattening so that those
    // passes can run on the more optimized control flow this pass creates in
    // many cases.
    //
    // FIXME: This should ideally be put after the LoadStoreVectorizer.
    // However, due to some annoying facts about ResourceUsageAnalysis,
    // (especially as exercised in the resource-usage-dead-function test),
    // we need all the function passes codegenprepare all the way through
    // said resource usage analysis to run on the call graph produced
    // before codegenprepare runs (because codegenprepare will knock some
    // nodes out of the graph, which leads to function-level passes not
    // being run on them, which causes crashes in the resource usage analysis).
    addPass(createAMDGPULowerBufferFatPointersPass());
    // In accordance with the above FIXME, manually force all the
    // function-level passes into a CGSCCPassManager.
    addPass(new DummyCGSCCPass());
  }

  TargetPassConfig::addCodeGenPrepare();

  if (isPassEnabled(EnableLoadStoreVectorizer))
    addPass(createLoadStoreVectorizerPass());

  // LowerSwitch pass may introduce unreachable blocks that can
  // cause unexpected behavior for subsequent passes. Placing it
  // here seems better that these blocks would get cleaned up by
  // UnreachableBlockElim inserted next in the pass flow.
  addPass(createLowerSwitchPass());
}

bool AMDGPUPassConfig::addPreISel() {
  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createFlattenCFGPass());
  return false;
}

bool AMDGPUPassConfig::addInstSelector() {
  addPass(createAMDGPUISelDag(getAMDGPUTargetMachine(), getOptLevel()));
  return false;
}

bool AMDGPUPassConfig::addGCPasses() {
  // Do nothing. GC is not supported.
  return false;
}

//===----------------------------------------------------------------------===//
// GCN Legacy Pass Setup
//===----------------------------------------------------------------------===//

bool GCNPassConfig::addPreISel() {
  AMDGPUPassConfig::addPreISel();

  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createSinkingPass());

  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createAMDGPULateCodeGenPrepareLegacyPass());

  // Merge divergent exit nodes. StructurizeCFG won't recognize the multi-exit
  // regions formed by them.
  addPass(&AMDGPUUnifyDivergentExitNodesID);
  addPass(createFixIrreduciblePass());
  addPass(createUnifyLoopExitsPass());
  addPass(createStructurizeCFGPass(false)); // true -> SkipUniformRegions

  addPass(createAMDGPUAnnotateUniformValuesLegacy());
  addPass(createSIAnnotateControlFlowLegacyPass());
  // TODO: Move this right after structurizeCFG to avoid extra divergence
  // analysis. This depends on stopping SIAnnotateControlFlow from making
  // control flow modifications.
  addPass(createAMDGPURewriteUndefForPHILegacyPass());

  // SDAG requires LCSSA, GlobalISel does not. Disable LCSSA for -global-isel
  // with -new-reg-bank-select and without any of the fallback options.
  if (!getCGPassBuilderOption().EnableGlobalISelOption ||
      !isGlobalISelAbortEnabled() || !NewRegBankSelect)
    addPass(createLCSSAPass());

  if (TM->getOptLevel() > CodeGenOptLevel::Less)
    addPass(&AMDGPUPerfHintAnalysisLegacyID);

  return false;
}

void GCNPassConfig::addMachineSSAOptimization() {
  TargetPassConfig::addMachineSSAOptimization();

  // We want to fold operands after PeepholeOptimizer has run (or as part of
  // it), because it will eliminate extra copies making it easier to fold the
  // real source operand. We want to eliminate dead instructions after, so that
  // we see fewer uses of the copies. We then need to clean up the dead
  // instructions leftover after the operands are folded as well.
  //
  // XXX - Can we get away without running DeadMachineInstructionElim again?
  addPass(&SIFoldOperandsLegacyID);
  if (EnableDPPCombine)
    addPass(&GCNDPPCombineLegacyID);
  addPass(&SILoadStoreOptimizerLegacyID);
  if (isPassEnabled(EnableSDWAPeephole)) {
    addPass(&SIPeepholeSDWALegacyID);
    addPass(&EarlyMachineLICMID);
    addPass(&MachineCSELegacyID);
    addPass(&SIFoldOperandsLegacyID);
  }
  addPass(&DeadMachineInstructionElimID);
  addPass(createSIShrinkInstructionsLegacyPass());
}

bool GCNPassConfig::addILPOpts() {
  if (EnableEarlyIfConversion)
    addPass(&EarlyIfConverterLegacyID);

  TargetPassConfig::addILPOpts();
  return false;
}

bool GCNPassConfig::addInstSelector() {
  AMDGPUPassConfig::addInstSelector();
  addPass(&SIFixSGPRCopiesLegacyID);
  addPass(createSILowerI1CopiesLegacyPass());
  return false;
}

bool GCNPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void GCNPassConfig::addPreLegalizeMachineIR() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPUPreLegalizeCombiner(IsOptNone));
  addPass(new Localizer());
}

bool GCNPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

void GCNPassConfig::addPreRegBankSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPUPostLegalizeCombiner(IsOptNone));
  addPass(createAMDGPUGlobalISelDivergenceLoweringPass());
}

bool GCNPassConfig::addRegBankSelect() {
  if (NewRegBankSelect) {
    addPass(createAMDGPURegBankSelectPass());
    addPass(createAMDGPURegBankLegalizePass());
  } else {
    addPass(new RegBankSelect());
  }
  return false;
}

void GCNPassConfig::addPreGlobalInstructionSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPURegBankCombiner(IsOptNone));
}

bool GCNPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}

void GCNPassConfig::addFastRegAlloc() {
  // FIXME: We have to disable the verifier here because of PHIElimination +
  // TwoAddressInstructions disabling it.

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass(&PHIEliminationID, &SILowerControlFlowLegacyID);

  insertPass(&TwoAddressInstructionPassID, &SIWholeQuadModeID);

  TargetPassConfig::addFastRegAlloc();
}

void GCNPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&AMDGPUPrepareAGPRAllocLegacyID);
}

void GCNPassConfig::addOptimizedRegAlloc() {
  if (EnableDCEInRA)
    insertPass(&DetectDeadLanesID, &DeadMachineInstructionElimID);

  // FIXME: when an instruction has a Killed operand, and the instruction is
  // inside a bundle, seems only the BUNDLE instruction appears as the Kills of
  // the register in LiveVariables, this would trigger a failure in verifier,
  // we should fix it and enable the verifier.
  if (OptVGPRLiveRange)
    insertPass(&LiveVariablesID, &SIOptimizeVGPRLiveRangeLegacyID);

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass(&PHIEliminationID, &SILowerControlFlowLegacyID);

  if (EnableRewritePartialRegUses)
    insertPass(&RenameIndependentSubregsID, &GCNRewritePartialRegUsesID);

  if (isPassEnabled(EnablePreRAOptimizations))
    insertPass(&MachineSchedulerID, &GCNPreRAOptimizationsID);

  // Allow the scheduler to run before SIWholeQuadMode inserts exec manipulation
  // instructions that cause scheduling barriers.
  insertPass(&MachineSchedulerID, &SIWholeQuadModeID);

  if (OptExecMaskPreRA)
    insertPass(&MachineSchedulerID, &SIOptimizeExecMaskingPreRAID);

  // This is not an essential optimization and it has a noticeable impact on
  // compilation time, so we only enable it from O2.
  if (TM->getOptLevel() > CodeGenOptLevel::Less)
    insertPass(&MachineSchedulerID, &SIFormMemoryClausesID);

  TargetPassConfig::addOptimizedRegAlloc();
}

bool GCNPassConfig::addPreRewrite() {
  if (EnableRegReassign)
    addPass(&GCNNSAReassignID);

  addPass(&AMDGPURewriteAGPRCopyMFMALegacyID);
  return true;
}

FunctionPass *GCNPassConfig::createSGPRAllocPass(bool Optimized) {
  // Initialize the global default.
  llvm::call_once(InitializeDefaultSGPRRegisterAllocatorFlag,
                  initializeDefaultSGPRRegisterAllocatorOnce);

  RegisterRegAlloc::FunctionPassCtor Ctor = SGPRRegisterRegAlloc::getDefault();
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyRegisterAllocator(onlyAllocateSGPRs);

  return createFastRegisterAllocator(onlyAllocateSGPRs, false);
}

FunctionPass *GCNPassConfig::createVGPRAllocPass(bool Optimized) {
  // Initialize the global default.
  llvm::call_once(InitializeDefaultVGPRRegisterAllocatorFlag,
                  initializeDefaultVGPRRegisterAllocatorOnce);

  RegisterRegAlloc::FunctionPassCtor Ctor = VGPRRegisterRegAlloc::getDefault();
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyVGPRRegisterAllocator();

  return createFastVGPRRegisterAllocator();
}

FunctionPass *GCNPassConfig::createWWMRegAllocPass(bool Optimized) {
  // Initialize the global default.
  llvm::call_once(InitializeDefaultWWMRegisterAllocatorFlag,
                  initializeDefaultWWMRegisterAllocatorOnce);

  RegisterRegAlloc::FunctionPassCtor Ctor = WWMRegisterRegAlloc::getDefault();
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyWWMRegisterAllocator();

  return createFastWWMRegisterAllocator();
}

FunctionPass *GCNPassConfig::createRegAllocPass(bool Optimized) {
  llvm_unreachable("should not be used");
}

static const char RegAllocOptNotSupportedMessage[] =
    "-regalloc not supported with amdgcn. Use -sgpr-regalloc, -wwm-regalloc, "
    "and -vgpr-regalloc";

bool GCNPassConfig::addRegAssignAndRewriteFast() {
  if (!usingDefaultRegAlloc())
    reportFatalUsageError(RegAllocOptNotSupportedMessage);

  addPass(&GCNPreRALongBranchRegID);

  addPass(createSGPRAllocPass(false));

  // Equivalent of PEI for SGPRs.
  addPass(&SILowerSGPRSpillsLegacyID);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addPass(&SIPreAllocateWWMRegsLegacyID);

  // For allocating other wwm register operands.
  addPass(createWWMRegAllocPass(false));

  addPass(&SILowerWWMCopiesLegacyID);
  addPass(&AMDGPUReserveWWMRegsLegacyID);

  // For allocating per-thread VGPRs.
  addPass(createVGPRAllocPass(false));

  return true;
}

bool GCNPassConfig::addRegAssignAndRewriteOptimized() {
  if (!usingDefaultRegAlloc())
    reportFatalUsageError(RegAllocOptNotSupportedMessage);

  addPass(&GCNPreRALongBranchRegID);

  addPass(createSGPRAllocPass(true));

  // Commit allocated register changes. This is mostly necessary because too
  // many things rely on the use lists of the physical registers, such as the
  // verifier. This is only necessary with allocators which use LiveIntervals,
  // since FastRegAlloc does the replacements itself.
  addPass(createVirtRegRewriter(false));

  // At this point, the sgpr-regalloc has been done and it is good to have the
  // stack slot coloring to try to optimize the SGPR spill stack indices before
  // attempting the custom SGPR spill lowering.
  addPass(&StackSlotColoringID);

  // Equivalent of PEI for SGPRs.
  addPass(&SILowerSGPRSpillsLegacyID);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addPass(&SIPreAllocateWWMRegsLegacyID);

  // For allocating other whole wave mode registers.
  addPass(createWWMRegAllocPass(true));
  addPass(&SILowerWWMCopiesLegacyID);
  addPass(createVirtRegRewriter(false));
  addPass(&AMDGPUReserveWWMRegsLegacyID);

  // For allocating per-thread VGPRs.
  addPass(createVGPRAllocPass(true));

  addPreRewrite();
  addPass(&VirtRegRewriterID);

  addPass(&AMDGPUMarkLastScratchLoadID);

  return true;
}

void GCNPassConfig::addPostRegAlloc() {
  addPass(&SIFixVGPRCopiesID);
  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIOptimizeExecMaskingLegacyID);
  TargetPassConfig::addPostRegAlloc();
}

void GCNPassConfig::addPreSched2() {
  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createSIShrinkInstructionsLegacyPass());
  addPass(&SIPostRABundlerLegacyID);
}

void GCNPassConfig::addPreEmitPass() {
  if (isPassEnabled(EnableVOPD, CodeGenOptLevel::Less))
    addPass(&GCNCreateVOPDID);
  addPass(createSIMemoryLegalizerPass());
  addPass(createSIInsertWaitcntsPass());

  addPass(createSIModeRegisterPass());

  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIInsertHardClausesID);

  addPass(&SILateBranchLoweringPassID);
  if (isPassEnabled(EnableSetWavePriority, CodeGenOptLevel::Less))
    addPass(createAMDGPUSetWavePriorityPass());
  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIPreEmitPeepholeID);
  // The hazard recognizer that runs as part of the post-ra scheduler does not
  // guarantee to be able handle all hazards correctly. This is because if there
  // are multiple scheduling regions in a basic block, the regions are scheduled
  // bottom up, so when we begin to schedule a region we don't know what
  // instructions were emitted directly before it.
  //
  // Here we add a stand-alone hazard recognizer pass which can handle all
  // cases.
  addPass(&PostRAHazardRecognizerID);

  addPass(&AMDGPUWaitSGPRHazardsLegacyID);

  if (isPassEnabled(EnableInsertDelayAlu, CodeGenOptLevel::Less))
    addPass(&AMDGPUInsertDelayAluID);

  addPass(&BranchRelaxationPassID);
}

void GCNPassConfig::addPostBBSections() {
  // We run this later to avoid passes like livedebugvalues and BBSections
  // having to deal with the apparent multi-entry functions we may generate.
  addPass(createAMDGPUPreloadKernArgPrologLegacyPass());
}

TargetPassConfig *GCNTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new GCNPassConfig(*this, PM);
}

void GCNTargetMachine::registerMachineRegisterInfoCallback(
    MachineFunction &MF) const {
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MF.getRegInfo().addDelegate(MFI);
}

MachineFunctionInfo *GCNTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return SIMachineFunctionInfo::create<SIMachineFunctionInfo>(
      Allocator, F, static_cast<const GCNSubtarget *>(STI));
}

yaml::MachineFunctionInfo *GCNTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::SIMachineFunctionInfo();
}

yaml::MachineFunctionInfo *
GCNTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  return new yaml::SIMachineFunctionInfo(
      *MFI, *MF.getSubtarget<GCNSubtarget>().getRegisterInfo(), MF);
}

bool GCNTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI_, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const yaml::SIMachineFunctionInfo &YamlMFI =
      static_cast<const yaml::SIMachineFunctionInfo &>(MFI_);
  MachineFunction &MF = PFS.MF;
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  if (MFI->initializeBaseYamlFields(YamlMFI, MF, PFS, Error, SourceRange))
    return true;

  if (MFI->Occupancy == 0) {
    // Fixup the subtarget dependent default value.
    MFI->Occupancy = ST.getOccupancyWithWorkGroupSizes(MF).second;
  }

  auto parseRegister = [&](const yaml::StringValue &RegName, Register &RegVal) {
    Register TempReg;
    if (parseNamedRegisterReference(PFS, TempReg, RegName.Value, Error)) {
      SourceRange = RegName.SourceRange;
      return true;
    }
    RegVal = TempReg;

    return false;
  };

  auto parseOptionalRegister = [&](const yaml::StringValue &RegName,
                                   Register &RegVal) {
    return !RegName.Value.empty() && parseRegister(RegName, RegVal);
  };

  if (parseOptionalRegister(YamlMFI.VGPRForAGPRCopy, MFI->VGPRForAGPRCopy))
    return true;

  if (parseOptionalRegister(YamlMFI.SGPRForEXECCopy, MFI->SGPRForEXECCopy))
    return true;

  if (parseOptionalRegister(YamlMFI.LongBranchReservedReg,
                            MFI->LongBranchReservedReg))
    return true;

  auto diagnoseRegisterClass = [&](const yaml::StringValue &RegName) {
    // Create a diagnostic for a the register string literal.
    const MemoryBuffer &Buffer =
        *PFS.SM->getMemoryBuffer(PFS.SM->getMainFileID());
    Error = SMDiagnostic(*PFS.SM, SMLoc(), Buffer.getBufferIdentifier(), 1,
                         RegName.Value.size(), SourceMgr::DK_Error,
                         "incorrect register class for field", RegName.Value,
                         {}, {});
    SourceRange = RegName.SourceRange;
    return true;
  };

  if (parseRegister(YamlMFI.ScratchRSrcReg, MFI->ScratchRSrcReg) ||
      parseRegister(YamlMFI.FrameOffsetReg, MFI->FrameOffsetReg) ||
      parseRegister(YamlMFI.StackPtrOffsetReg, MFI->StackPtrOffsetReg))
    return true;

  if (MFI->ScratchRSrcReg != AMDGPU::PRIVATE_RSRC_REG &&
      !AMDGPU::SGPR_128RegClass.contains(MFI->ScratchRSrcReg)) {
    return diagnoseRegisterClass(YamlMFI.ScratchRSrcReg);
  }

  if (MFI->FrameOffsetReg != AMDGPU::FP_REG &&
      !AMDGPU::SGPR_32RegClass.contains(MFI->FrameOffsetReg)) {
    return diagnoseRegisterClass(YamlMFI.FrameOffsetReg);
  }

  if (MFI->StackPtrOffsetReg != AMDGPU::SP_REG &&
      !AMDGPU::SGPR_32RegClass.contains(MFI->StackPtrOffsetReg)) {
    return diagnoseRegisterClass(YamlMFI.StackPtrOffsetReg);
  }

  for (const auto &YamlReg : YamlMFI.WWMReservedRegs) {
    Register ParsedReg;
    if (parseRegister(YamlReg, ParsedReg))
      return true;

    MFI->reserveWWMRegister(ParsedReg);
  }

  for (const auto &[_, Info] : PFS.VRegInfosNamed) {
    MFI->setFlag(Info->VReg, Info->Flags);
  }
  for (const auto &[_, Info] : PFS.VRegInfos) {
    MFI->setFlag(Info->VReg, Info->Flags);
  }

  for (const auto &YamlRegStr : YamlMFI.SpillPhysVGPRS) {
    Register ParsedReg;
    if (parseRegister(YamlRegStr, ParsedReg))
      return true;
    MFI->SpillPhysVGPRs.push_back(ParsedReg);
  }

  auto parseAndCheckArgument = [&](const std::optional<yaml::SIArgument> &A,
                                   const TargetRegisterClass &RC,
                                   ArgDescriptor &Arg, unsigned UserSGPRs,
                                   unsigned SystemSGPRs) {
    // Skip parsing if it's not present.
    if (!A)
      return false;

    if (A->IsRegister) {
      Register Reg;
      if (parseNamedRegisterReference(PFS, Reg, A->RegisterName.Value, Error)) {
        SourceRange = A->RegisterName.SourceRange;
        return true;
      }
      if (!RC.contains(Reg))
        return diagnoseRegisterClass(A->RegisterName);
      Arg = ArgDescriptor::createRegister(Reg);
    } else
      Arg = ArgDescriptor::createStack(A->StackOffset);
    // Check and apply the optional mask.
    if (A->Mask)
      Arg = ArgDescriptor::createArg(Arg, *A->Mask);

    MFI->NumUserSGPRs += UserSGPRs;
    MFI->NumSystemSGPRs += SystemSGPRs;
    return false;
  };

  if (YamlMFI.ArgInfo &&
      (parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentBuffer,
                             AMDGPU::SGPR_128RegClass,
                             MFI->ArgInfo.PrivateSegmentBuffer, 4, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->DispatchPtr,
                             AMDGPU::SReg_64RegClass, MFI->ArgInfo.DispatchPtr,
                             2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->QueuePtr, AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.QueuePtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->KernargSegmentPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.KernargSegmentPtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->DispatchID,
                             AMDGPU::SReg_64RegClass, MFI->ArgInfo.DispatchID,
                             2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->FlatScratchInit,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.FlatScratchInit, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentSize,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.PrivateSegmentSize, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->LDSKernelId,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.LDSKernelId, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDX,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDX,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDY,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDY,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDZ,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDZ,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupInfo,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.WorkGroupInfo, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentWaveByteOffset,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.PrivateSegmentWaveByteOffset, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->ImplicitArgPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.ImplicitArgPtr, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->ImplicitBufferPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.ImplicitBufferPtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDX,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDX, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDY,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDY, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDZ,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDZ, 0, 0)))
    return true;

  if (ST.hasIEEEMode())
    MFI->Mode.IEEE = YamlMFI.Mode.IEEE;
  if (ST.hasDX10ClampMode())
    MFI->Mode.DX10Clamp = YamlMFI.Mode.DX10Clamp;

  // FIXME: Move proper support for denormal-fp-math into base MachineFunction
  MFI->Mode.FP32Denormals.Input = YamlMFI.Mode.FP32InputDenormals
                                      ? DenormalMode::IEEE
                                      : DenormalMode::PreserveSign;
  MFI->Mode.FP32Denormals.Output = YamlMFI.Mode.FP32OutputDenormals
                                       ? DenormalMode::IEEE
                                       : DenormalMode::PreserveSign;

  MFI->Mode.FP64FP16Denormals.Input = YamlMFI.Mode.FP64FP16InputDenormals
                                          ? DenormalMode::IEEE
                                          : DenormalMode::PreserveSign;
  MFI->Mode.FP64FP16Denormals.Output = YamlMFI.Mode.FP64FP16OutputDenormals
                                           ? DenormalMode::IEEE
                                           : DenormalMode::PreserveSign;

  if (YamlMFI.HasInitWholeWave)
    MFI->setInitWholeWave();

  return false;
}

//===----------------------------------------------------------------------===//
// AMDGPU CodeGen Pass Builder interface.
//===----------------------------------------------------------------------===//

AMDGPUCodeGenPassBuilder::AMDGPUCodeGenPassBuilder(
    GCNTargetMachine &TM, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC)
    : CodeGenPassBuilder(TM, Opts, PIC) {
  Opt.MISchedPostRA = true;
  Opt.RequiresCodeGenSCCOrder = true;
  // Exceptions and StackMaps are not supported, so these passes will never do
  // anything.
  // Garbage collection is not supported.
  disablePass<StackMapLivenessPass, FuncletLayoutPass,
              ShadowStackGCLoweringPass>();
}

void AMDGPUCodeGenPassBuilder::addIRPasses(AddIRPass &addPass) const {
  if (RemoveIncompatibleFunctions && TM.getTargetTriple().isAMDGCN())
    addPass(AMDGPURemoveIncompatibleFunctionsPass(TM));

  addPass(AMDGPUPrintfRuntimeBindingPass());
  if (LowerCtorDtor)
    addPass(AMDGPUCtorDtorLoweringPass());

  if (isPassEnabled(EnableImageIntrinsicOptimizer))
    addPass(AMDGPUImageIntrinsicOptimizerPass(TM));

  // This can be disabled by passing ::Disable here or on the command line
  // with --expand-variadics-override=disable.
  addPass(ExpandVariadicsPass(ExpandVariadicsMode::Lowering));

  addPass(AMDGPUAlwaysInlinePass());
  addPass(AlwaysInlinerPass());

  addPass(AMDGPUExportKernelRuntimeHandlesPass());

  if (EnableSwLowerLDS)
    addPass(AMDGPUSwLowerLDSPass(TM));

  // Runs before PromoteAlloca so the latter can account for function uses
  if (EnableLowerModuleLDS)
    addPass(AMDGPULowerModuleLDSPass(TM));

  // Run atomic optimizer before Atomic Expand
  if (TM.getOptLevel() >= CodeGenOptLevel::Less &&
      (AMDGPUAtomicOptimizerStrategy != ScanOptions::None))
    addPass(AMDGPUAtomicOptimizerPass(TM, AMDGPUAtomicOptimizerStrategy));

  addPass(AtomicExpandPass(&TM));

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addPass(AMDGPUPromoteAllocaPass(TM));
    if (isPassEnabled(EnableScalarIRPasses))
      addStraightLineScalarOptimizationPasses(addPass);

    // TODO: Handle EnableAMDGPUAliasAnalysis

    // TODO: May want to move later or split into an early and late one.
    addPass(AMDGPUCodeGenPreparePass(TM));

    // TODO: LICM
  }

  Base::addIRPasses(addPass);

  // EarlyCSE is not always strong enough to clean up what LSR produces. For
  // example, GVN can combine
  //
  //   %0 = add %a, %b
  //   %1 = add %b, %a
  //
  // and
  //
  //   %0 = shl nsw %a, 2
  //   %1 = shl %a, 2
  //
  // but EarlyCSE can do neither of them.
  if (isPassEnabled(EnableScalarIRPasses))
    addEarlyCSEOrGVNPass(addPass);
}

void AMDGPUCodeGenPassBuilder::addCodeGenPrepare(AddIRPass &addPass) const {
  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addPass(AMDGPUPreloadKernelArgumentsPass(TM));

  if (EnableLowerKernelArguments)
    addPass(AMDGPULowerKernelArgumentsPass(TM));

  // This lowering has been placed after codegenprepare to take advantage of
  // address mode matching (which is why it isn't put with the LDS lowerings).
  // It could be placed anywhere before uniformity annotations (an analysis
  // that it changes by splitting up fat pointers into their components)
  // but has been put before switch lowering and CFG flattening so that those
  // passes can run on the more optimized control flow this pass creates in
  // many cases.
  //
  // FIXME: This should ideally be put after the LoadStoreVectorizer.
  // However, due to some annoying facts about ResourceUsageAnalysis,
  // (especially as exercised in the resource-usage-dead-function test),
  // we need all the function passes codegenprepare all the way through
  // said resource usage analysis to run on the call graph produced
  // before codegenprepare runs (because codegenprepare will knock some
  // nodes out of the graph, which leads to function-level passes not
  // being run on them, which causes crashes in the resource usage analysis).
  addPass(AMDGPULowerBufferFatPointersPass(TM));

  addPass.requireCGSCCOrder();

  Base::addCodeGenPrepare(addPass);

  if (isPassEnabled(EnableLoadStoreVectorizer))
    addPass(LoadStoreVectorizerPass());

  // LowerSwitch pass may introduce unreachable blocks that can cause unexpected
  // behavior for subsequent passes. Placing it here seems better that these
  // blocks would get cleaned up by UnreachableBlockElim inserted next in the
  // pass flow.
  addPass(LowerSwitchPass());
}

void AMDGPUCodeGenPassBuilder::addPreISel(AddIRPass &addPass) const {

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addPass(FlattenCFGPass());
    addPass(SinkingPass());
    addPass(AMDGPULateCodeGenPreparePass(TM));
  }

  // Merge divergent exit nodes. StructurizeCFG won't recognize the multi-exit
  // regions formed by them.

  addPass(AMDGPUUnifyDivergentExitNodesPass());
  addPass(FixIrreduciblePass());
  addPass(UnifyLoopExitsPass());
  addPass(StructurizeCFGPass(/*SkipUniformRegions=*/false));

  addPass(AMDGPUAnnotateUniformValuesPass());

  addPass(SIAnnotateControlFlowPass(TM));

  // TODO: Move this right after structurizeCFG to avoid extra divergence
  // analysis. This depends on stopping SIAnnotateControlFlow from making
  // control flow modifications.
  addPass(AMDGPURewriteUndefForPHIPass());

  if (!getCGPassBuilderOption().EnableGlobalISelOption ||
      !isGlobalISelAbortEnabled() || !NewRegBankSelect)
    addPass(LCSSAPass());

  if (TM.getOptLevel() > CodeGenOptLevel::Less)
    addPass(AMDGPUPerfHintAnalysisPass(TM));

  // FIXME: Why isn't this queried as required from AMDGPUISelDAGToDAG, and why
  // isn't this in addInstSelector?
  addPass(RequireAnalysisPass<UniformityInfoAnalysis, Function>(),
          /*Force=*/true);
}

void AMDGPUCodeGenPassBuilder::addILPOpts(AddMachinePass &addPass) const {
  if (EnableEarlyIfConversion)
    addPass(EarlyIfConverterPass());

  Base::addILPOpts(addPass);
}

void AMDGPUCodeGenPassBuilder::addAsmPrinter(AddMachinePass &addPass,
                                             CreateMCStreamer) const {
  // TODO: Add AsmPrinter.
}

Error AMDGPUCodeGenPassBuilder::addInstSelector(AddMachinePass &addPass) const {
  addPass(AMDGPUISelDAGToDAGPass(TM));
  addPass(SIFixSGPRCopiesPass());
  addPass(SILowerI1CopiesPass());
  return Error::success();
}

void AMDGPUCodeGenPassBuilder::addPreRewrite(AddMachinePass &addPass) const {
  if (EnableRegReassign) {
    addPass(GCNNSAReassignPass());
  }
}

void AMDGPUCodeGenPassBuilder::addMachineSSAOptimization(
    AddMachinePass &addPass) const {
  Base::addMachineSSAOptimization(addPass);

  addPass(SIFoldOperandsPass());
  if (EnableDPPCombine) {
    addPass(GCNDPPCombinePass());
  }
  addPass(SILoadStoreOptimizerPass());
  if (isPassEnabled(EnableSDWAPeephole)) {
    addPass(SIPeepholeSDWAPass());
    addPass(EarlyMachineLICMPass());
    addPass(MachineCSEPass());
    addPass(SIFoldOperandsPass());
  }
  addPass(DeadMachineInstructionElimPass());
  addPass(SIShrinkInstructionsPass());
}

void AMDGPUCodeGenPassBuilder::addOptimizedRegAlloc(
    AddMachinePass &addPass) const {
  if (EnableDCEInRA)
    insertPass<DetectDeadLanesPass>(DeadMachineInstructionElimPass());

  // FIXME: when an instruction has a Killed operand, and the instruction is
  // inside a bundle, seems only the BUNDLE instruction appears as the Kills of
  // the register in LiveVariables, this would trigger a failure in verifier,
  // we should fix it and enable the verifier.
  if (OptVGPRLiveRange)
    insertPass<RequireAnalysisPass<LiveVariablesAnalysis, MachineFunction>>(
        SIOptimizeVGPRLiveRangePass());

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass<PHIEliminationPass>(SILowerControlFlowPass());

  if (EnableRewritePartialRegUses)
    insertPass<RenameIndependentSubregsPass>(GCNRewritePartialRegUsesPass());

  if (isPassEnabled(EnablePreRAOptimizations))
    insertPass<MachineSchedulerPass>(GCNPreRAOptimizationsPass());

  // Allow the scheduler to run before SIWholeQuadMode inserts exec manipulation
  // instructions that cause scheduling barriers.
  insertPass<MachineSchedulerPass>(SIWholeQuadModePass());

  if (OptExecMaskPreRA)
    insertPass<MachineSchedulerPass>(SIOptimizeExecMaskingPreRAPass());

  // This is not an essential optimization and it has a noticeable impact on
  // compilation time, so we only enable it from O2.
  if (TM.getOptLevel() > CodeGenOptLevel::Less)
    insertPass<MachineSchedulerPass>(SIFormMemoryClausesPass());

  Base::addOptimizedRegAlloc(addPass);
}

void AMDGPUCodeGenPassBuilder::addPreRegAlloc(AddMachinePass &addPass) const {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(AMDGPUPrepareAGPRAllocPass());
}

Error AMDGPUCodeGenPassBuilder::addRegAssignmentOptimized(
    AddMachinePass &addPass) const {
  // TODO: Check --regalloc-npm option

  addPass(GCNPreRALongBranchRegPass());

  addPass(RAGreedyPass({onlyAllocateSGPRs, "sgpr"}));

  // Commit allocated register changes. This is mostly necessary because too
  // many things rely on the use lists of the physical registers, such as the
  // verifier. This is only necessary with allocators which use LiveIntervals,
  // since FastRegAlloc does the replacements itself.
  addPass(VirtRegRewriterPass(false));

  // At this point, the sgpr-regalloc has been done and it is good to have the
  // stack slot coloring to try to optimize the SGPR spill stack indices before
  // attempting the custom SGPR spill lowering.
  addPass(StackSlotColoringPass());

  // Equivalent of PEI for SGPRs.
  addPass(SILowerSGPRSpillsPass());

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addPass(SIPreAllocateWWMRegsPass());

  // For allocating other wwm register operands.
  addPass(RAGreedyPass({onlyAllocateWWMRegs, "wwm"}));
  addPass(SILowerWWMCopiesPass());
  addPass(VirtRegRewriterPass(false));
  addPass(AMDGPUReserveWWMRegsPass());

  // For allocating per-thread VGPRs.
  addPass(RAGreedyPass({onlyAllocateVGPRs, "vgpr"}));


  addPreRewrite(addPass);
  addPass(VirtRegRewriterPass(true));

  addPass(AMDGPUMarkLastScratchLoadPass());
  return Error::success();
}

void AMDGPUCodeGenPassBuilder::addPostRegAlloc(AddMachinePass &addPass) const {
  addPass(SIFixVGPRCopiesPass());
  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addPass(SIOptimizeExecMaskingPass());
  Base::addPostRegAlloc(addPass);
}

void AMDGPUCodeGenPassBuilder::addPreSched2(AddMachinePass &addPass) const {
  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addPass(SIShrinkInstructionsPass());
  addPass(SIPostRABundlerPass());
}

void AMDGPUCodeGenPassBuilder::addPreEmitPass(AddMachinePass &addPass) const {
  if (isPassEnabled(EnableVOPD, CodeGenOptLevel::Less)) {
    addPass(GCNCreateVOPDPass());
  }

  addPass(SIMemoryLegalizerPass());
  addPass(SIInsertWaitcntsPass());

  // TODO: addPass(SIModeRegisterPass());

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    // TODO: addPass(SIInsertHardClausesPass());
  }

  addPass(SILateBranchLoweringPass());

  if (isPassEnabled(EnableSetWavePriority, CodeGenOptLevel::Less))
    addPass(AMDGPUSetWavePriorityPass());

  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addPass(SIPreEmitPeepholePass());

  // The hazard recognizer that runs as part of the post-ra scheduler does not
  // guarantee to be able handle all hazards correctly. This is because if there
  // are multiple scheduling regions in a basic block, the regions are scheduled
  // bottom up, so when we begin to schedule a region we don't know what
  // instructions were emitted directly before it.
  //
  // Here we add a stand-alone hazard recognizer pass which can handle all
  // cases.
  addPass(PostRAHazardRecognizerPass());
  addPass(AMDGPUWaitSGPRHazardsPass());

  if (isPassEnabled(EnableInsertDelayAlu, CodeGenOptLevel::Less)) {
    addPass(AMDGPUInsertDelayAluPass());
  }

  addPass(BranchRelaxationPass());
}

bool AMDGPUCodeGenPassBuilder::isPassEnabled(const cl::opt<bool> &Opt,
                                             CodeGenOptLevel Level) const {
  if (Opt.getNumOccurrences())
    return Opt;
  if (TM.getOptLevel() < Level)
    return false;
  return Opt;
}

void AMDGPUCodeGenPassBuilder::addEarlyCSEOrGVNPass(AddIRPass &addPass) const {
  if (TM.getOptLevel() == CodeGenOptLevel::Aggressive)
    addPass(GVNPass());
  else
    addPass(EarlyCSEPass());
}

void AMDGPUCodeGenPassBuilder::addStraightLineScalarOptimizationPasses(
    AddIRPass &addPass) const {
  if (isPassEnabled(EnableLoopPrefetch, CodeGenOptLevel::Aggressive))
    addPass(LoopDataPrefetchPass());

  addPass(SeparateConstOffsetFromGEPPass());

  // ReassociateGEPs exposes more opportunities for SLSR. See
  // the example in reassociate-geps-and-slsr.ll.
  addPass(StraightLineStrengthReducePass());

  // SeparateConstOffsetFromGEP and SLSR creates common expressions which GVN or
  // EarlyCSE can reuse.
  addEarlyCSEOrGVNPass(addPass);

  // Run NaryReassociate after EarlyCSE/GVN to be more effective.
  addPass(NaryReassociatePass());

  // NaryReassociate on GEPs creates redundant common expressions, so run
  // EarlyCSE after it.
  addPass(EarlyCSEPass());
}
