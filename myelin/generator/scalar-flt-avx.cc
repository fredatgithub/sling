#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate scalar float expression using AXV and XMM registers.
class ScalarFltAVXGenerator : public ExpressionGenerator {
 public:
  ScalarFltAVXGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg_reg = true;
    model_.op_reg_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model_.fm_reg_reg_reg = true;
      model_.fm_reg_reg_mem = true;
    }
  }

  string Name() override { return "ScalarFltAVX"; }

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
            &Assembler::vaddss, &Assembler::vaddsd,
            &Assembler::vaddss, &Assembler::vaddsd,
            masm);
        break;
      case Express::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::vsubss, &Assembler::vsubsd,
            &Assembler::vsubss, &Assembler::vsubsd,
            masm);
        break;
      case Express::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::vmulss, &Assembler::vmulsd,
            &Assembler::vmulss, &Assembler::vmulsd,
            masm);
        break;
      case Express::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::vdivss, &Assembler::vdivsd,
            &Assembler::vdivss, &Assembler::vdivsd,
            masm);
        break;
      case Express::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::vminss, &Assembler::vminsd,
            &Assembler::vminss, &Assembler::vminsd,
            masm);
        break;
      case Express::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::vmaxss, &Assembler::vmaxsd,
            &Assembler::vmaxss, &Assembler::vmaxsd,
            masm);
        break;
      case Express::RELU:
        GenerateRelu(instr, masm);
        break;
      case Express::MULADD132:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd132ss, &Assembler::vfmadd132sd,
            &Assembler::vfmadd132ss, &Assembler::vfmadd132sd,
            masm);
        break;
      case Express::MULADD213:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd213ss, &Assembler::vfmadd213sd,
            &Assembler::vfmadd213ss, &Assembler::vfmadd213sd,
            masm);
        break;
      case Express::MULADD231:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd231ss, &Assembler::vfmadd231sd,
            &Assembler::vfmadd231ss, &Assembler::vfmadd231sd,
            masm);
        break;
      case Express::MULSUB132:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub132ss, &Assembler::vfmsub132sd,
            &Assembler::vfmsub132ss, &Assembler::vfmsub132sd,
            masm);
        break;
      case Express::MULSUB213:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub213ss, &Assembler::vfmsub213sd,
            &Assembler::vfmsub213ss, &Assembler::vfmsub213sd,
            masm);
        break;
      case Express::MULSUB231:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub231ss, &Assembler::vfmsub231sd,
            &Assembler::vfmsub231ss, &Assembler::vfmsub231sd,
            masm);
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate relu(x) = max(0,x).
  void GenerateRelu(Express::Op *instr, MacroAssembler *masm) {
    __ vpxor(xmm(instr->dst), xmm(instr->dst), xmm(instr->dst));
    switch (type_) {
      case DT_FLOAT:
        if (instr->dst != -1 && instr->src != -1) {
          __ vmaxss(xmm(instr->dst), xmm(instr->dst), xmm(instr->src));
        } else if (instr->dst != -1 && instr->src == -1) {
          __ vmaxss(xmm(instr->dst), xmm(instr->dst), addr(instr->args[1]));
        } else {
          UNSUPPORTED;
        }
        break;
      case DT_DOUBLE:
        if (instr->dst != -1 && instr->src != -1) {
          __ vmaxsd(xmm(instr->dst), xmm(instr->dst), xmm(instr->src));
        } else if (instr->dst != -1 && instr->src == -1) {
          __ vmaxsd(xmm(instr->dst), xmm(instr->dst), addr(instr->args[1]));
        } else {
          UNSUPPORTED;
        }
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateScalarFltAVXGenerator() {
  return new ScalarFltAVXGenerator();
}

}  // namespace myelin
}  // namespace sling
