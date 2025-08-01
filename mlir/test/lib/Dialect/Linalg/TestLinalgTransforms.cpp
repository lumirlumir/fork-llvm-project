//===- TestLinalgTransforms.cpp - Test Linalg transformation patterns -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements logic for testing Linalg transformations.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Bufferization/IR/Bufferization.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/Linalg/Transforms/Hoisting.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/Linalg/Utils/Utils.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/SmallVector.h"

using namespace mlir;
using namespace mlir::linalg;

namespace {
struct TestLinalgTransforms
    : public PassWrapper<TestLinalgTransforms, OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TestLinalgTransforms)

  TestLinalgTransforms() = default;
  TestLinalgTransforms(const TestLinalgTransforms &pass) : PassWrapper(pass) {}

  void getDependentDialects(DialectRegistry &registry) const override {
    // clang-format off
    registry.insert<affine::AffineDialect,
                    bufferization::BufferizationDialect,
                    memref::MemRefDialect,
                    scf::SCFDialect,
                    linalg::LinalgDialect,
                    vector::VectorDialect,
                    gpu::GPUDialect>();
    // clang-format on
  }
  StringRef getArgument() const final {
    return "test-linalg-transform-patterns";
  }
  StringRef getDescription() const final {
    return "Test Linalg transformation patterns by applying them greedily.";
  }

  void runOnOperation() override;

  Option<bool> testPatterns{*this, "test-patterns",
                            llvm::cl::desc("Test a mixed set of patterns"),
                            llvm::cl::init(false)};
  Option<bool> testVectorTransferForwardingPatterns{
      *this, "test-vector-transfer-forwarding-patterns",
      llvm::cl::desc(
          "Test a fused pass that forwards memref.copy to vector.transfer"),
      llvm::cl::init(false)};
  Option<bool> testDecomposePadTensor{
      *this, "test-decompose-pad-tensor",
      llvm::cl::desc("Test transform pad tensor by copying with generic ops"),
      llvm::cl::init(false)};
  // TODO: This is not used - delete.
  Option<bool> testDecomposeTensorPackOp{
      *this, "test-decompose-linalg-pack",
      llvm::cl::desc("Test transform that generalizes pack ops into a sequence "
                     "of tensor and Linalg ops"),
      llvm::cl::init(false)};
  Option<bool> testDecomposeTensorUnPackOp{
      *this, "test-decompose-tensor-unpack",
      llvm::cl::desc(
          "Test transform that generalizes unpack ops into a sequence "
          "of tensor and Linalg ops"),
      llvm::cl::init(false)};
  Option<bool> testSwapSubTensorPadTensor{
      *this, "test-swap-subtensor-padtensor",
      llvm::cl::desc("Test rewrite of subtensor(tensor.pad) into "
                     "tensor.pad(subtensor)"),
      llvm::cl::init(false)};
  ListOption<int64_t> peeledLoops{
      *this, "peeled-loops",
      llvm::cl::desc("Loops to be peeled when test-tile-pattern")};
  ListOption<int64_t> tileSizes{
      *this, "tile-sizes",
      llvm::cl::desc("Linalg tile sizes for test-tile-pattern")};
  Option<bool> skipPartial{
      *this, "skip-partial",
      llvm::cl::desc("Skip loops inside partial iterations during peeling"),
      llvm::cl::init(false)};
  Option<std::string> loopType{
      *this, "loop-type",
      llvm::cl::desc("Specify the type of loops to generate: for, parallel or "
                     "tiled_loop"),
      llvm::cl::init("for")};
  Option<bool> testBubbleUpExtractSliceOpPattern{
      *this, "test-bubble-up-extract-slice-op-pattern",
      llvm::cl::desc("Test rewrite of linalgOp + extract_slice into "
                     "extract_slice + linalgOp"),
      llvm::cl::init(false)};
  Option<bool> testSwapExtractSliceWithFill{
      *this, "test-swap-extract-slice-with-fill-pattern",
      llvm::cl::desc(
          "Test patterns to swap tensor.extract_slice(linalg.fill())"),
      llvm::cl::init(false)};
  Option<bool> testEraseUnusedOperandsAndResults{
      *this, "test-erase-unused-operands-and-results",
      llvm::cl::desc("Test patterns to erase unused operands and results"),
      llvm::cl::init(false)};
  Option<bool> testEraseUnnecessaryInputs{
      *this, "test-erase-unnecessary-inputs",
      llvm::cl::desc("Test patterns to erase unnecessary inputs"),
      llvm::cl::init(false)};
  Option<bool> testWinogradConv2D{
      *this, "test-winograd-conv2d",
      llvm::cl::desc("Test transform conv2d by Winograd conv2d algorithm"),
      llvm::cl::init(false)};
  Option<bool> testDecomposeWinogradOps{
      *this, "test-decompose-winograd-ops",
      llvm::cl::desc("Test decompose Winograd ops"), llvm::cl::init(false)};
  Option<bool> testFoldIntoPackAndUnpack{
      *this, "test-fold-into-pack-and-unpack",
      llvm::cl::desc("Test folding ops into linalg.pack and linalg.unpack"),
      llvm::cl::init(false)};
  Option<bool> testFoldIntoPackAndUnpackWithControlFn{
      *this, "test-fold-into-pack-and-unpack-control",
      llvm::cl::desc(
          "Test controlling folding ops into linalg.pack and linalg.unpack"),
      llvm::cl::init(false)};
  Option<bool> testSimplifyPackUnpackPatterns{
      *this, "test-simplify-pack-unpack-patterns",
      llvm::cl::desc("Test patterns to simplify linalg.pack and linalg.unpack"),
      llvm::cl::init(false)};
};
} // namespace

static void applyPatterns(func::FuncOp funcOp) {
  MLIRContext *ctx = funcOp.getContext();
  RewritePatternSet patterns(ctx);

  //===--------------------------------------------------------------------===//
  // Linalg distribution patterns.
  //===--------------------------------------------------------------------===//
  LinalgLoopDistributionOptions distributionOptions;

  //===--------------------------------------------------------------------===//
  // Linalg to vector contraction patterns.
  //===--------------------------------------------------------------------===//
  patterns.add<CopyVectorizationPattern>(ctx);

  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyVectorTransferForwardingPatterns(func::FuncOp funcOp) {
  RewritePatternSet forwardPattern(funcOp.getContext());
  forwardPattern.add<LinalgCopyVTRForwardingPattern>(funcOp.getContext());
  forwardPattern.add<LinalgCopyVTWForwardingPattern>(funcOp.getContext());
  (void)applyPatternsGreedily(funcOp, std::move(forwardPattern));
}

static void applyDecomposePadPatterns(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  patterns.add<DecomposePadOpPattern>(funcOp.getContext());
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyDecomposeTensorPackPatterns(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  patterns.add<DecomposeOuterUnitDimsPackOpPattern>(funcOp.getContext());
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyDecomposeTensorUnPackPatterns(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  patterns.add<DecomposeOuterUnitDimsUnPackOpPattern>(funcOp.getContext());
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyExtractSliceOfPadTensorSwapPattern(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  patterns.add<ExtractSliceOfPadTensorSwapPattern>(funcOp.getContext());
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyBubbleUpExtractSliceOpPattern(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateBubbleUpExtractSliceOpPatterns(patterns);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applySwapExtractSliceWithFillPattern(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateSwapExtractSliceWithFillPatterns(patterns);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyEraseUnusedOperandsAndResultsPatterns(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateEraseUnusedOperandsAndResultsPatterns(patterns);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyEraseUnnecessaryInputs(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateEraseUnnecessaryInputsPatterns(patterns);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyWinogradConv2D(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateWinogradConv2DPatterns(patterns, WinogradConv2DFmr::F_4_3);
  populateWinogradConv2DPatterns(patterns, WinogradConv2DFmr::F_2_5);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyDecomposeWinogradOps(func::FuncOp funcOp) {
  RewritePatternSet patterns(funcOp.getContext());
  populateDecomposeWinogradOpsPatterns(patterns);
  (void)applyPatternsGreedily(funcOp, std::move(patterns));
}

static void applyFoldIntoPackAndUnpackPatterns(
    Operation *rootOp,
    linalg::ControlFoldIntoPackUnpackFn controlFn = nullptr) {
  RewritePatternSet patterns(rootOp->getContext());
  linalg::populateFoldIntoPackAndUnpackPatterns(patterns, controlFn);
  (void)applyPatternsGreedily(rootOp, std::move(patterns));
}

static void applySimplifyPackUnpackPatterns(Operation *rootOp) {
  RewritePatternSet patterns(rootOp->getContext());
  linalg::populateSimplifyPackAndUnpackPatterns(patterns);
  (void)applyPatternsGreedily(rootOp, std::move(patterns));
}

/// Apply transformations specified as patterns.
void TestLinalgTransforms::runOnOperation() {
  if (testPatterns)
    return applyPatterns(getOperation());
  if (testVectorTransferForwardingPatterns)
    return applyVectorTransferForwardingPatterns(getOperation());
  if (testDecomposePadTensor)
    return applyDecomposePadPatterns(getOperation());
  if (testDecomposeTensorPackOp)
    return applyDecomposeTensorPackPatterns(getOperation());
  if (testDecomposeTensorUnPackOp)
    return applyDecomposeTensorUnPackPatterns(getOperation());
  if (testSwapSubTensorPadTensor)
    return applyExtractSliceOfPadTensorSwapPattern(getOperation());
  if (testBubbleUpExtractSliceOpPattern)
    return applyBubbleUpExtractSliceOpPattern(getOperation());
  if (testSwapExtractSliceWithFill)
    return applySwapExtractSliceWithFillPattern(getOperation());
  if (testEraseUnusedOperandsAndResults)
    return applyEraseUnusedOperandsAndResultsPatterns(getOperation());
  if (testEraseUnnecessaryInputs)
    return applyEraseUnnecessaryInputs(getOperation());
  if (testWinogradConv2D)
    return applyWinogradConv2D(getOperation());
  if (testDecomposeWinogradOps)
    return applyDecomposeWinogradOps(getOperation());
  Operation *rootOp = getOperation();
  if (testFoldIntoPackAndUnpack)
    applyFoldIntoPackAndUnpackPatterns(rootOp);
  if (testFoldIntoPackAndUnpackWithControlFn) {
    linalg::ControlFoldIntoPackUnpackFn controlFn = [](OpOperand *opOperand) {
      Operation *producer = opOperand->get().getDefiningOp();
      Operation *consumer = opOperand->getOwner();
      // If we have a pack/unpack consumer and a producer that has multiple
      // uses, do not apply the folding patterns.
      if (isa<linalg::PackOp, linalg::UnPackOp>(consumer) &&
          isa<TilingInterface>(producer) && !producer->hasOneUse())
        return false;
      return true;
    };
    applyFoldIntoPackAndUnpackPatterns(rootOp, controlFn);
  }
  if (testSimplifyPackUnpackPatterns)
    applySimplifyPackUnpackPatterns(rootOp);
}

namespace mlir {
namespace test {
void registerTestLinalgTransforms() {
  PassRegistration<TestLinalgTransforms>();
}
} // namespace test
} // namespace mlir
