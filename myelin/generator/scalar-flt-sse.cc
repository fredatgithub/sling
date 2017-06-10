#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate scalar float expression using SSE and XMM registers.
class ScalarFltSSEGenerator : public ExpressionGenerator {
 public:
  ScalarFltSSEGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "ScalarFltSSE"; }

  void Reserve() override {
    // Reserve XMM registers.
    index_->ReserveXMMRegisters(instructions_.NumRegs());
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        GenerateXMMScalarFltMove(instr, masm);
        break;
      case Express::ADD:
        GenerateXMMFltOp(instr,
            &Assembler::addss, &Assembler::addsd,
            &Assembler::addss, &Assembler::addsd,
            masm);
        break;
      case Express::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::subss, &Assembler::subsd,
            &Assembler::subss, &Assembler::subsd,
            masm);
        break;
      case Express::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::mulss, &Assembler::mulsd,
            &Assembler::mulss, &Assembler::mulsd,
            masm);
        break;
      case Express::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::divss, &Assembler::divsd,
            &Assembler::divss, &Assembler::divsd,
            masm);
        break;
      case Express::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::minss, &Assembler::minsd,
            &Assembler::minss, &Assembler::minsd,
            masm);
        break;
      case Express::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::maxss, &Assembler::maxsd,
            &Assembler::maxss, &Assembler::maxsd,
            masm);
        break;
      case Express::RELU:
        GenerateRelu(instr, masm);
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate relu(x) = max(0,x).
  void GenerateRelu(Express::Op *instr, MacroAssembler *masm) {
    if (type_ == DT_FLOAT) {
      __ xorps(xmm(instr->dst), xmm(instr->dst));
    } else if (type_ == DT_DOUBLE) {
      if (CPU::Enabled(SSE2)) {
        __ xorpd(xmm(instr->dst), xmm(instr->dst));
      } else {
        __ xorps(xmm(instr->dst), xmm(instr->dst));
      }
    } else {
      UNSUPPORTED;
    }
    GenerateXMMFltOp(instr,
        &Assembler::maxss, &Assembler::maxsd,
        &Assembler::maxss, &Assembler::maxsd,
        masm);
  }
};

ExpressionGenerator *CreateScalarFltSSEGenerator() {
  return new ScalarFltSSEGenerator();
}

}  // namespace myelin
}  // namespace sling
