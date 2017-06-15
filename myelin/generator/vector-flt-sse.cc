#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector float expression using SSE and XMM registers.
class VectorFltSSEGenerator : public ExpressionGenerator {
 public:
  VectorFltSSEGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_imm = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_imm = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "VectorFltSSE"; }

  int VectorSize() override { return XMMRegSize; }

  void Reserve() override {
    // Reserve XMM registers.
    index_->ReserveXMMRegisters(instructions_.NumRegs());
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        if (IsClear(instr)) {
          // Use XOR to zero register instead of loading constant from memory.
          switch (type_) {
            case DT_FLOAT:
              __ xorps(xmm(instr->dst), xmm(instr->dst));
              break;
            case DT_DOUBLE:
              __ xorpd(xmm(instr->dst), xmm(instr->dst));
              break;
            default: UNSUPPORTED;
          }
        } else {
          GenerateXMMVectorMove(instr, masm);
        }
        break;
      case Express::ADD:
        GenerateXMMFltOp(instr,
            &Assembler::addps, &Assembler::addpd,
            &Assembler::addps, &Assembler::addpd,
            masm);
        break;
      case Express::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::subps, &Assembler::subpd,
            &Assembler::subps, &Assembler::subpd,
            masm);
        break;
      case Express::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::mulps, &Assembler::mulpd,
            &Assembler::mulps, &Assembler::mulpd,
            masm);
        break;
      case Express::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::divps, &Assembler::divpd,
            &Assembler::divps, &Assembler::divpd,
            masm);
        break;
      case Express::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::minps, &Assembler::minpd,
            &Assembler::minps, &Assembler::minpd,
            masm);
        break;
      case Express::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::maxps, &Assembler::maxpd,
            &Assembler::maxps, &Assembler::maxpd,
            masm);
        break;
      case Express::RELU:
        GenerateRelu(instr, masm);
        break;
      case Express::CMPEQOQ:
        GenerateCompare(instr, masm, 0);
        break;
      case Express::CMPLTOQ:
        GenerateCompare(instr, masm, 17);
        break;
      case Express::CMPGTOQ:
        GenerateCompare(instr, masm, 30);
        break;
      case Express::CMPNGEUQ:
        GenerateCompare(instr, masm, 25);
        break;
      case Express::AND:
        GenerateXMMFltOp(instr,
            &Assembler::andps, &Assembler::andpd,
            &Assembler::andps, &Assembler::andpd,
            masm);
        break;
      case Express::OR:
        GenerateXMMFltOp(instr,
            &Assembler::orps, &Assembler::orpd,
            &Assembler::orps, &Assembler::orpd,
            masm);
        break;
      case Express::ANDNOT:
        if (CPU::Enabled(SSE2)) {
          GenerateXMMFltOp(instr,
              &Assembler::andnps, &Assembler::andnpd,
              &Assembler::andnps, &Assembler::andnpd,
              masm);
        } else {
          UNSUPPORTED;
        }
        break;
      case Express::SHR23:
        GenerateShift(instr, masm, false, 23);
        break;
      case Express::SHL23:
        GenerateShift(instr, masm, true, 23);
        break;
      case Express::FLOOR:
        GenerateFloor(instr, masm);
        break;
      case Express::CVTFLTINT:
        GenerateFltToInt(instr, masm);
        break;
      case Express::CVTINTFLT:
        GenerateIntToFlt(instr, masm);
        break;
      case Express::SUBINT:
        GenerateXMMFltOp(instr,
            &Assembler::psubd, &Assembler::psubq,
            &Assembler::psubd, &Assembler::psubq,
            masm);
        break;
      default:
        LOG(INFO) << "Unsupported: " << instr->AsInstruction();
        UNSUPPORTED;
    }
  }

  // Generate relu.
  void GenerateRelu(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE2)) {
      if (type_ == DT_FLOAT) {
        __ xorps(xmm(instr->dst), xmm(instr->dst));
      } else if (type_ == DT_DOUBLE) {
        __ xorpd(xmm(instr->dst), xmm(instr->dst));
      } else {
        UNSUPPORTED;
      }
    } else if (type_ == DT_FLOAT) {
      float zero = 0;
      auto *data = masm->CreateDataBlock(sizeof(float));
      data->Add(zero);
      __ movss(xmm(instr->dst), data->address());
    } else {
      UNSUPPORTED;
    }
    GenerateXMMFltOp(instr,
        &Assembler::maxps, &Assembler::maxpd,
        &Assembler::maxps, &Assembler::maxpd,
        masm);
  }

  // Generate left/right shift.
  void GenerateShift(Express::Op *instr, MacroAssembler *masm,
                     int left, int bits) {
    // Move argument to destination register
    CHECK(instr->dst != -1);
    if (instr->src != -1) {
      __ movapd(xmm(instr->dst), xmm(instr->src));
    } else {
      switch (type_) {
        case DT_FLOAT:
          __ movaps(xmm(instr->dst), addr(instr->args[0]));
          break;
        case DT_DOUBLE:
          __ movapd(xmm(instr->dst), addr(instr->args[0]));
          break;
        default: UNSUPPORTED;
      }
    }

    // Shift xmm register.
    switch (type_) {
      case DT_FLOAT:
        if (CPU::Enabled(SSE2)) {
          if (left) {
            __ pslld(xmm(instr->dst), bits);
          } else {
            __ psrld(xmm(instr->dst), bits);
          }
        } else {
          UNSUPPORTED;
        }
        break;
      case DT_DOUBLE:
        if (CPU::Enabled(SSE2)) {
          if (left) {
            __ psllq(xmm(instr->dst), bits);
          } else {
            __ psrlq(xmm(instr->dst), bits);
          }
        } else {
          UNSUPPORTED;
        }
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate floor rounding.
  void GenerateFloor(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE4_1)) {
      GenerateXMMFltOp(instr,
          &Assembler::roundps, &Assembler::roundpd,
          &Assembler::roundps, &Assembler::roundpd,
          kRoundDown, masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate float to integer conversion.
  void GenerateFltToInt(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE2)) {
      GenerateXMMFltOp(instr,
          &Assembler::cvttps2dq, &Assembler::cvttpd2dq,
          &Assembler::cvttps2dq, &Assembler::cvttpd2dq,
          masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate integer to float conversion.
  void GenerateIntToFlt(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE2)) {
      GenerateXMMFltOp(instr,
          &Assembler::cvtdq2ps, &Assembler::cvtdq2pd,
          &Assembler::cvtdq2ps, &Assembler::cvtdq2pd,
          masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate compare.
  void GenerateCompare(Express::Op *instr, MacroAssembler *masm, int8 code) {
    GenerateXMMFltOp(instr,
        &Assembler::cmpps, &Assembler::cmppd,
        &Assembler::cmpps, &Assembler::cmppd,
        code, masm);
  }
};

ExpressionGenerator *CreateVectorFltSSEGenerator() {
  return new VectorFltSSEGenerator();
}

}  // namespace myelin
}  // namespace sling

