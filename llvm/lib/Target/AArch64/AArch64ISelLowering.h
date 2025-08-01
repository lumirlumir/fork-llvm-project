//==-- AArch64ISelLowering.h - AArch64 DAG Lowering Interface ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that AArch64 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64ISELLOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64ISELLOWERING_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

namespace AArch64 {
/// Possible values of current rounding mode, which is specified in bits
/// 23:22 of FPCR.
enum Rounding {
  RN = 0,    // Round to Nearest
  RP = 1,    // Round towards Plus infinity
  RM = 2,    // Round towards Minus infinity
  RZ = 3,    // Round towards Zero
  rmMask = 3 // Bit mask selecting rounding mode
};

// Bit position of rounding mode bits in FPCR.
const unsigned RoundingBitsPos = 22;

// Reserved bits should be preserved when modifying FPCR.
const uint64_t ReservedFPControlBits = 0xfffffffff80040f8;

// Registers used to pass function arguments.
ArrayRef<MCPhysReg> getGPRArgRegs();
ArrayRef<MCPhysReg> getFPRArgRegs();

/// Maximum allowed number of unprobed bytes above SP at an ABI
/// boundary.
const unsigned StackProbeMaxUnprobedStack = 1024;

/// Maximum number of iterations to unroll for a constant size probing loop.
const unsigned StackProbeMaxLoopUnroll = 4;

} // namespace AArch64

namespace ARM64AS {
enum : unsigned { PTR32_SPTR = 270, PTR32_UPTR = 271, PTR64 = 272 };
}

class AArch64Subtarget;

class AArch64TargetLowering : public TargetLowering {
public:
  explicit AArch64TargetLowering(const TargetMachine &TM,
                                 const AArch64Subtarget &STI);

  /// Control the following reassociation of operands: (op (op x, c1), y) -> (op
  /// (op x, y), c1) where N0 is (op x, c1) and N1 is y.
  bool isReassocProfitable(SelectionDAG &DAG, SDValue N0,
                           SDValue N1) const override;

  /// Selects the correct CCAssignFn for a given CallingConvention value.
  CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg) const;

  /// Selects the correct CCAssignFn for a given CallingConvention value.
  CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC) const;

  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets.
  void computeKnownBitsForTargetNode(const SDValue Op, KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth = 0) const override;

  unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                           const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           unsigned Depth) const override;

  MVT getPointerTy(const DataLayout &DL, uint32_t AS = 0) const override {
    if ((AS == ARM64AS::PTR32_SPTR) || (AS == ARM64AS::PTR32_UPTR)) {
      // These are 32-bit pointers created using the `__ptr32` extension or
      // similar. They are handled by marking them as being in a different
      // address space, and will be extended to 64-bits when used as the target
      // of a load or store operation, or cast to a 64-bit pointer type.
      return MVT::i32;
    } else {
      // Returning i64 unconditionally here (i.e. even for ILP32) means that the
      // *DAG* representation of pointers will always be 64-bits. They will be
      // truncated and extended when transferred to memory, but the 64-bit DAG
      // allows us to use AArch64's addressing modes much more easily.
      return MVT::i64;
    }
  }

  unsigned getVectorIdxWidth(const DataLayout &DL) const override {
    // The VectorIdx type is i64, with both normal and ilp32.
    return 64;
  }

  bool targetShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                                    const APInt &DemandedElts,
                                    TargetLoweringOpt &TLO) const override;

  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;

  /// Returns true if the target allows unaligned memory accesses of the
  /// specified type.
  bool allowsMisalignedMemoryAccesses(
      EVT VT, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const override;
  /// LLT variant.
  bool allowsMisalignedMemoryAccesses(LLT Ty, unsigned AddrSpace,
                                      Align Alignment,
                                      MachineMemOperand::Flags Flags,
                                      unsigned *Fast = nullptr) const override;

  /// Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  /// This method returns a target specific FastISel object, or null if the
  /// target does not support "fast" ISel.
  FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                           const TargetLibraryInfo *libInfo) const override;

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;

  /// Return true if the given shuffle mask can be codegen'd directly, or if it
  /// should be stack expanded.
  bool isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const override;

  /// Similar to isShuffleMaskLegal. Return true is the given 'select with zero'
  /// shuffle mask can be codegen'd directly.
  bool isVectorClearMaskLegal(ArrayRef<int> M, EVT VT) const override;

  /// Return the ISD::SETCC ValueType.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  SDValue ReconstructShuffle(SDValue Op, SelectionDAG &DAG) const;

  MachineBasicBlock *EmitF128CSEL(MachineInstr &MI,
                                  MachineBasicBlock *BB) const;

  MachineBasicBlock *EmitLoweredCatchRet(MachineInstr &MI,
                                           MachineBasicBlock *BB) const;

  MachineBasicBlock *EmitDynamicProbedAlloc(MachineInstr &MI,
                                            MachineBasicBlock *MBB) const;

  MachineBasicBlock *EmitTileLoad(unsigned Opc, unsigned BaseReg,
                                  MachineInstr &MI,
                                  MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitFill(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitZAInstr(unsigned Opc, unsigned BaseReg,
                                 MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitZTInstr(MachineInstr &MI, MachineBasicBlock *BB,
                                 unsigned Opcode, bool Op0IsDef) const;
  MachineBasicBlock *EmitZero(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitInitTPIDR2Object(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitAllocateZABuffer(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitAllocateSMESaveBuffer(MachineInstr &MI,
                                               MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitGetSMESaveSize(MachineInstr &MI,
                                        MachineBasicBlock *BB) const;

  /// Replace (0, vreg) discriminator components with the operands of blend
  /// or with (immediate, NoRegister) when possible.
  void fixupPtrauthDiscriminator(MachineInstr &MI, MachineBasicBlock *BB,
                                 MachineOperand &IntDiscOp,
                                 MachineOperand &AddrDiscOp,
                                 const TargetRegisterClass *AddrDiscRC) const;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;

  bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy, EVT NewVT,
                             std::optional<unsigned> ByteOffset) const override;

  bool shouldRemoveRedundantExtend(SDValue Op) const override;

  bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
  bool isTruncateFree(EVT VT1, EVT VT2) const override;

  bool isProfitableToHoist(Instruction *I) const override;

  bool isZExtFree(Type *Ty1, Type *Ty2) const override;
  bool isZExtFree(EVT VT1, EVT VT2) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;

  bool optimizeExtendOrTruncateConversion(
      Instruction *I, Loop *L, const TargetTransformInfo &TTI) const override;

  bool hasPairedLoad(EVT LoadedType, Align &RequiredAlignment) const override;

  unsigned getMaxSupportedInterleaveFactor() const override { return 4; }

  bool lowerInterleavedLoad(Instruction *Load, Value *Mask,
                            ArrayRef<ShuffleVectorInst *> Shuffles,
                            ArrayRef<unsigned> Indices,
                            unsigned Factor) const override;
  bool lowerInterleavedStore(Instruction *Store, Value *Mask,
                             ShuffleVectorInst *SVI,
                             unsigned Factor) const override;

  bool lowerDeinterleaveIntrinsicToLoad(Instruction *Load, Value *Mask,
                                        IntrinsicInst *DI) const override;

  bool lowerInterleaveIntrinsicToStore(
      Instruction *Store, Value *Mask,
      ArrayRef<Value *> InterleaveValues) const override;

  bool isLegalAddImmediate(int64_t) const override;
  bool isLegalAddScalableImmediate(int64_t) const override;
  bool isLegalICmpImmediate(int64_t) const override;

  bool isMulAddWithConstProfitable(SDValue AddNode,
                                   SDValue ConstNode) const override;

  bool shouldConsiderGEPOffsetSplit() const override;

  EVT getOptimalMemOpType(LLVMContext &Context, const MemOp &Op,
                          const AttributeList &FuncAttributes) const override;

  LLT getOptimalMemOpLLT(const MemOp &Op,
                         const AttributeList &FuncAttributes) const override;

  /// Return true if the addressing mode represented by AM is legal for this
  /// target, for a load/store of the specified type.
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;

  int64_t getPreferredLargeGEPBaseOffset(int64_t MinOffset,
                                         int64_t MaxOffset) const override;

  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override;
  bool isFMAFasterThanFMulAndFAdd(const Function &F, Type *Ty) const override;

  bool generateFMAsInMachineCombiner(EVT VT,
                                     CodeGenOptLevel OptLevel) const override;

  /// Return true if the target has native support for
  /// the specified value type and it is 'desirable' to use the type for the
  /// given node type.
  bool isTypeDesirableForOp(unsigned Opc, EVT VT) const override;

  const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const override;
  ArrayRef<MCPhysReg> getRoundingControlRegisters() const override;

  /// Returns false if N is a bit extraction pattern of (X >> C) & Mask.
  bool isDesirableToCommuteWithShift(const SDNode *N,
                                     CombineLevel Level) const override;

  bool isDesirableToPullExtFromShl(const MachineInstr &MI) const override {
    return false;
  }

  /// Returns false if N is a bit extraction pattern of (X >> C) & Mask.
  bool isDesirableToCommuteXorWithShift(const SDNode *N) const override;

  /// Return true if it is profitable to fold a pair of shifts into a mask.
  bool shouldFoldConstantShiftPairToMask(const SDNode *N,
                                         CombineLevel Level) const override;

  bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode, EVT VT,
                                            unsigned SelectOpcode, SDValue X,
                                            SDValue Y) const override;

  /// Returns true if it is beneficial to convert a load of a constant
  /// to just the constant itself.
  bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                         Type *Ty) const override;

  /// Return true if EXTRACT_SUBVECTOR is cheap for this result type
  /// with this index.
  bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                               unsigned Index) const override;

  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    // Using overflow ops for overflow checks only should beneficial on
    // AArch64.
    return TargetLowering::shouldFormOverflowOp(Opcode, VT, true);
  }

  Value *emitLoadLinked(IRBuilderBase &Builder, Type *ValueTy, Value *Addr,
                        AtomicOrdering Ord) const override;
  Value *emitStoreConditional(IRBuilderBase &Builder, Value *Val, Value *Addr,
                              AtomicOrdering Ord) const override;

  void emitAtomicCmpXchgNoStoreLLBalance(IRBuilderBase &Builder) const override;

  bool isOpSuitableForLDPSTP(const Instruction *I) const;
  bool isOpSuitableForLSE128(const Instruction *I) const;
  bool isOpSuitableForRCPC3(const Instruction *I) const;
  bool shouldInsertFencesForAtomic(const Instruction *I) const override;
  bool
  shouldInsertTrailingFenceForAtomicStore(const Instruction *I) const override;

  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;

  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const override;

  bool useLoadStackGuardNode(const Module &M) const override;
  TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const override;

  /// If the target has a standard location for the stack protector cookie,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getIRStackGuard(IRBuilderBase &IRB) const override;

  void insertSSPDeclarations(Module &M) const override;
  Value *getSDagStackGuard(const Module &M) const override;
  Function *getSSPStackGuardCheck(const Module &M) const override;

  /// If the target has a standard location for the unsafe stack pointer,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getSafeStackPointerLocation(IRBuilderBase &IRB) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;

  bool canMergeStoresTo(unsigned AddressSpace, EVT MemVT,
                        const MachineFunction &MF) const override;

  bool isCheapToSpeculateCttz(Type *) const override {
    return true;
  }

  bool isCheapToSpeculateCtlz(Type *) const override {
    return true;
  }

  bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override;

  bool hasAndNotCompare(SDValue V) const override {
    // We can use bics for any scalar.
    return V.getValueType().isScalarInteger();
  }

  bool hasAndNot(SDValue Y) const override {
    EVT VT = Y.getValueType();

    if (!VT.isVector())
      return hasAndNotCompare(Y);

    if (VT.isScalableVector())
      return true;

    return VT.getFixedSizeInBits() >= 64; // vector 'bic'
  }

  bool shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
      SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
      unsigned OldShiftOpcode, unsigned NewShiftOpcode,
      SelectionDAG &DAG) const override;

  ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const override;

  bool shouldTransformSignedTruncationCheck(EVT XVT,
                                            unsigned KeptBits) const override {
    // For vectors, we don't have a preference..
    if (XVT.isVector())
      return false;

    auto VTIsOk = [](EVT VT) -> bool {
      return VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32 ||
             VT == MVT::i64;
    };

    // We are ok with KeptBitsVT being byte/word/dword, what SXT supports.
    // XVT will be larger than KeptBitsVT.
    MVT KeptBitsVT = MVT::getIntegerVT(KeptBits);
    return VTIsOk(XVT) && VTIsOk(KeptBitsVT);
  }

  bool preferIncOfAddToSubOfNot(EVT VT) const override;

  bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const override;

  bool shouldExpandCmpUsingSelects(EVT VT) const override;

  bool isComplexDeinterleavingSupported() const override;
  bool isComplexDeinterleavingOperationSupported(
      ComplexDeinterleavingOperation Operation, Type *Ty) const override;

  Value *createComplexDeinterleavingIR(
      IRBuilderBase &B, ComplexDeinterleavingOperation OperationType,
      ComplexDeinterleavingRotation Rotation, Value *InputA, Value *InputB,
      Value *Accumulator = nullptr) const override;

  bool supportSplitCSR(MachineFunction *MF) const override {
    return MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
           MF->getFunction().hasFnAttribute(Attribute::NoUnwind);
  }
  void initializeSplitCSR(MachineBasicBlock *Entry) const override;
  void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const override;

  bool supportSwiftError() const override {
    return true;
  }

  bool supportPtrAuthBundles() const override { return true; }

  bool supportKCFIBundles() const override { return true; }

  MachineInstr *EmitKCFICheck(MachineBasicBlock &MBB,
                              MachineBasicBlock::instr_iterator &MBBI,
                              const TargetInstrInfo *TII) const override;

  /// Enable aggressive FMA fusion on targets that want it.
  bool enableAggressiveFMAFusion(EVT VT) const override;

  bool aggressivelyPreferBuildVectorSources(EVT VecVT) const override {
    return true;
  }

  /// Returns the size of the platform's va_list object.
  unsigned getVaListSizeInBits(const DataLayout &DL) const override;

  /// Returns true if \p VecTy is a legal interleaved access type. This
  /// function checks the vector element type and the overall width of the
  /// vector.
  bool isLegalInterleavedAccessType(VectorType *VecTy, const DataLayout &DL,
                                    bool &UseScalable) const;

  /// Returns the number of interleaved accesses that will be generated when
  /// lowering accesses of the given type.
  unsigned getNumInterleavedAccesses(VectorType *VecTy, const DataLayout &DL,
                                     bool UseScalable) const;

  MachineMemOperand::Flags getTargetMMOFlags(
    const Instruction &I) const override;

  bool functionArgumentNeedsConsecutiveRegisters(
      Type *Ty, CallingConv::ID CallConv, bool isVarArg,
      const DataLayout &DL) const override;

  /// Used for exception handling on Win64.
  bool needsFixedCatchObjects() const override;

  bool fallBackToDAGISel(const Instruction &Inst) const override;

  /// SVE code generation for fixed length vectors does not custom lower
  /// BUILD_VECTOR. This makes BUILD_VECTOR legalisation a source of stores to
  /// merge. However, merging them creates a BUILD_VECTOR that is just as
  /// illegal as the original, thus leading to an infinite legalisation loop.
  /// NOTE: Once BUILD_VECTOR is legal or can be custom lowered for all legal
  /// vector types this override can be removed.
  bool mergeStoresAfterLegalization(EVT VT) const override;

  // If the platform/function should have a redzone, return the size in bytes.
  unsigned getRedZoneSize(const Function &F) const {
    if (F.hasFnAttribute(Attribute::NoRedZone))
      return 0;
    return 128;
  }

  bool isAllActivePredicate(SelectionDAG &DAG, SDValue N) const;
  EVT getPromotedVTForPredicate(EVT VT) const;

  EVT getAsmOperandValueType(const DataLayout &DL, Type *Ty,
                             bool AllowUnknown = false) const override;

  bool shouldExpandGetActiveLaneMask(EVT VT, EVT OpVT) const override;

  bool
  shouldExpandPartialReductionIntrinsic(const IntrinsicInst *I) const override;

  bool shouldExpandCttzElements(EVT VT) const override;

  bool shouldExpandVectorMatch(EVT VT, unsigned SearchSize) const override;

  /// If a change in streaming mode is required on entry to/return from a
  /// function call it emits and returns the corresponding SMSTART or SMSTOP
  /// node. \p Condition should be one of the enum values from
  /// AArch64SME::ToggleCondition.
  SDValue changeStreamingMode(SelectionDAG &DAG, SDLoc DL, bool Enable,
                              SDValue Chain, SDValue InGlue, unsigned Condition,
                              SDValue PStateSM = SDValue()) const;

  bool isVScaleKnownToBeAPowerOfTwo() const override { return true; }

  // Normally SVE is only used for byte size vectors that do not fit within a
  // NEON vector. This changes when OverrideNEON is true, allowing SVE to be
  // used for 64bit and 128bit vectors as well.
  bool useSVEForFixedLengthVectorVT(EVT VT, bool OverrideNEON = false) const;

  // Follow NEON ABI rules even when using SVE for fixed length vectors.
  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override;
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CC,
                                         EVT VT) const override;
  unsigned getVectorTypeBreakdownForCallingConv(LLVMContext &Context,
                                                CallingConv::ID CC, EVT VT,
                                                EVT &IntermediateVT,
                                                unsigned &NumIntermediates,
                                                MVT &RegisterVT) const override;

  /// True if stack clash protection is enabled for this functions.
  bool hasInlineStackProbe(const MachineFunction &MF) const override;

  /// In AArch64, true if FEAT_CPA is present. Allows pointer arithmetic
  /// semantics to be preserved for instruction selection.
  bool shouldPreservePtrArith(const Function &F, EVT PtrVT) const override;

private:
  /// Keep a pointer to the AArch64Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const AArch64Subtarget *Subtarget;

  bool isExtFreeImpl(const Instruction *Ext) const override;

  void addTypeForNEON(MVT VT);
  void addTypeForFixedLengthSVE(MVT VT);
  void addDRType(MVT VT);
  void addQRType(MVT VT);

  bool shouldExpandBuildVectorWithShuffles(EVT, unsigned) const override;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                     SDNode *Node) const override;

  SDValue LowerCall(CallLoweringInfo & /*CLI*/,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool isVarArg,
                          const SmallVectorImpl<CCValAssign> &RVLocs,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
                          SDValue ThisVal, bool RequiresSMChange) const;

  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerStore128(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerABS(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerMGATHER(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerMSCATTER(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerMLOAD(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerVECTOR_COMPRESS(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerINTRINSIC_W_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG) const;

  bool
  isEligibleForTailCallOptimization(const CallLoweringInfo &CLI) const;

  /// Finds the incoming stack arguments which overlap the given fixed stack
  /// object and incorporates their load into the current chain. This prevents
  /// an upcoming store from clobbering the stack argument before it's used.
  SDValue addTokenForArgument(SDValue Chain, SelectionDAG &DAG,
                              MachineFrameInfo &MFI, int ClobberedFI) const;

  bool DoesCalleeRestoreStack(CallingConv::ID CallCC, bool TailCallOpt) const;

  void saveVarArgRegisters(CCState &CCInfo, SelectionDAG &DAG, const SDLoc &DL,
                           SDValue &Chain) const;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue getTargetNode(GlobalAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(JumpTableSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(ConstantPoolSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(BlockAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(ExternalSymbolSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  template <class NodeTy>
  SDValue getGOT(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddrLarge(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddrTiny(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  SDValue LowerADDROFRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDarwinGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerELFGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerELFTLSLocalExec(const GlobalValue *GV, SDValue ThreadBase,
                               const SDLoc &DL, SelectionDAG &DAG) const;
  SDValue LowerELFTLSDescCallSeq(SDValue SymAddr, const SDLoc &DL,
                                 SelectionDAG &DAG) const;
  SDValue LowerWindowsGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerPtrAuthGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCCCARRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(ISD::CondCode CC, SDValue LHS, SDValue RHS,
                         SDValue TVal, SDValue FVal,
                         iterator_range<SDNode::user_iterator> Users,
                         SDNodeFlags Flags, const SDLoc &dl,
                         SelectionDAG &DAG) const;
  SDValue LowerINIT_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerADJUST_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBRIND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAAPCS_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDarwin_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerWin64_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVACOPY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVAARG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSPONENTRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGET_FPMODE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSET_FPMODE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRESET_FPMODE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerZERO_EXTEND_VECTOR_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSPLAT_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDUPQLane(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerToPredicatedOp(SDValue Op, SelectionDAG &DAG,
                              unsigned NewOp) const;
  SDValue LowerToScalableOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_SPLICE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINSERT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_DEINTERLEAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_INTERLEAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_HISTOGRAM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerPARTIAL_REDUCE_MLA(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGET_ACTIVE_LANE_MASK(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDIV(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerMUL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorSRA_SRL_SHL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCTPOP_PARITY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCTTZ(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBitreverse(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerMinMax(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorXRINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerXOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBITCAST(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVSCALE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerTRUNCATE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECREDUCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerATOMIC_LOAD_AND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerWindowsDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerInlineDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerAVG(SDValue Op, SelectionDAG &DAG, unsigned NewOp) const;

  SDValue LowerFixedLengthVectorIntDivideToSVE(SDValue Op,
                                               SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorIntExtendToSVE(SDValue Op,
                                               SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorLoadToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorMLoadToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECREDUCE_SEQ_FADD(SDValue ScalarOp, SelectionDAG &DAG) const;
  SDValue LowerPredReductionToSVE(SDValue ScalarOp, SelectionDAG &DAG) const;
  SDValue LowerReductionToSVE(unsigned Opcode, SDValue ScalarOp,
                              SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorSelectToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorSetccToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorStoreToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorMStoreToSVE(SDValue Op,
                                            SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVectorTruncateToSVE(SDValue Op,
                                              SelectionDAG &DAG) const;
  SDValue LowerFixedLengthExtractVectorElt(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthInsertVectorElt(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthBitcastToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthConcatVectorsToSVE(SDValue Op,
                                             SelectionDAG &DAG) const;
  SDValue LowerFixedLengthFPExtendToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthFPRoundToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthIntToFPToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthFPToIntToSVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFixedLengthVECTOR_SHUFFLEToSVE(SDValue Op,
                                              SelectionDAG &DAG) const;
  SDValue LowerFixedLengthBuildVectorToSVE(SDValue Op, SelectionDAG &DAG) const;

  SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                        SmallVectorImpl<SDNode *> &Created) const override;
  SDValue BuildSREMPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                        SmallVectorImpl<SDNode *> &Created) const override;
  SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                          int &ExtraSteps, bool &UseOneConst,
                          bool Reciprocal) const override;
  SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &ExtraSteps) const override;
  SDValue getSqrtInputTest(SDValue Operand, SelectionDAG &DAG,
                           const DenormalMode &Mode) const override;
  SDValue getSqrtResultForDenormInput(SDValue Operand,
                                      SelectionDAG &DAG) const override;
  unsigned combineRepeatedFPDivisors() const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;
  Register getRegisterByName(const char* RegName, LLT VT,
                             const MachineFunction &MF) const override;

  /// Examine constraint string and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  ConstraintWeight
  getSingleConstraintMatchWeight(AsmOperandInfo &info,
                                 const char *constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  const char *LowerXConstraint(EVT ConstraintVT) const override;

  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
    if (ConstraintCode == "Q")
      return InlineAsm::ConstraintCode::Q;
    // FIXME: clang has code for 'Ump', 'Utf', 'Usa', and 'Ush' but these are
    //        followed by llvm_unreachable so we'll leave them unimplemented in
    //        the backend for now.
    return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
  }

  /// Handle Lowering flag assembly outputs.
  SDValue LowerAsmOutputForConstraint(SDValue &Chain, SDValue &Flag,
                                      const SDLoc &DL,
                                      const AsmOperandInfo &Constraint,
                                      SelectionDAG &DAG) const override;

  bool shouldExtendGSIndex(EVT VT, EVT &EltTy) const override;
  bool shouldRemoveExtendFromGSIndex(SDValue Extend, EVT DataVT) const override;
  bool isVectorLoadExtDesirable(SDValue ExtVal) const override;
  bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;
  bool getIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                              SDValue &Offset, SelectionDAG &DAG) const;
  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;
  bool isIndexingLegal(MachineInstr &MI, Register Base, Register Offset,
                       bool IsPre, MachineRegisterInfo &MRI) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  void ReplaceBITCASTResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                             SelectionDAG &DAG) const;
  void ReplaceExtractSubVectorResults(SDNode *N,
                                      SmallVectorImpl<SDValue> &Results,
                                      SelectionDAG &DAG) const;
  void ReplaceGetActiveLaneMaskResults(SDNode *N,
                                       SmallVectorImpl<SDValue> &Results,
                                       SelectionDAG &DAG) const;

  bool shouldNormalizeToSelectSequence(LLVMContext &, EVT) const override;

  void finalizeLowering(MachineFunction &MF) const override;

  bool shouldLocalize(const MachineInstr &MI,
                      const TargetTransformInfo *TTI) const override;

  bool SimplifyDemandedBitsForTargetNode(SDValue Op,
                                         const APInt &OriginalDemandedBits,
                                         const APInt &OriginalDemandedElts,
                                         KnownBits &Known,
                                         TargetLoweringOpt &TLO,
                                         unsigned Depth) const override;

  bool isTargetCanonicalConstantNode(SDValue Op) const override;

  // With the exception of data-predicate transitions, no instructions are
  // required to cast between legal scalable vector types. However:
  //  1. Packed and unpacked types have different bit lengths, meaning BITCAST
  //     is not universally useable.
  //  2. Most unpacked integer types are not legal and thus integer extends
  //     cannot be used to convert between unpacked and packed types.
  // These can make "bitcasting" a multiphase process. REINTERPRET_CAST is used
  // to transition between unpacked and packed types of the same element type,
  // with BITCAST used otherwise.
  // This function does not handle predicate bitcasts.
  SDValue getSVESafeBitCast(EVT VT, SDValue Op, SelectionDAG &DAG) const;

  // Returns the runtime value for PSTATE.SM by generating a call to
  // __arm_sme_state.
  SDValue getRuntimePStateSM(SelectionDAG &DAG, SDValue Chain, SDLoc DL,
                             EVT VT) const;

  bool preferScalarizeSplat(SDNode *N) const override;

  unsigned getMinimumJumpTableEntries() const override;

  bool softPromoteHalfType() const override { return true; }

  bool shouldScalarizeBinop(SDValue VecOp) const override {
    return VecOp.getOpcode() == ISD::SETCC;
  }
};

namespace AArch64 {
FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                         const TargetLibraryInfo *libInfo);
} // end namespace AArch64

} // end namespace llvm

#endif
