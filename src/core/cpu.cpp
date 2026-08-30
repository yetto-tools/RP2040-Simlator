#include "core/cpu.h"

#include "core/alu.h"
#include "core/timing.h"

namespace rp2040 {

namespace {

constexpr bool neg(std::uint32_t v) { return (v & 0x80000000u) != 0; }

std::uint32_t sxt8(std::uint32_t v) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int8_t>(static_cast<std::uint8_t>(v))));
}
std::uint32_t sxt16(std::uint32_t v) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(static_cast<std::uint16_t>(v))));
}

unsigned popcount16(std::uint16_t v) {
    unsigned c = 0;
    while (v != 0) {
        c += (v & 1u);
        v = static_cast<std::uint16_t>(v >> 1);
    }
    return c;
}

}  // namespace

std::uint32_t Cpu::read_reg(unsigned n, std::uint32_t instr_pc) const {
    if (n == 15) return instr_pc + 4u;  // ARMv6-M: PC reads as current + 4
    return regs_.get(n);
}

void Cpu::write_reg(unsigned n, std::uint32_t value) {
    if (n == 15) {
        regs_.set_pc(value & ~std::uint32_t{1});  // BranchWritePC: clear bit 0
        return;
    }
    regs_.set(n, value);
}

void Cpu::reset() {
    regs_.reset();
    pending_ = 0;
    const BusResult<std::uint32_t> sp = mem_.read_word(vtor_);
    const BusResult<std::uint32_t> pc = mem_.read_word(vtor_ + 4u);
    if (sp.ok()) regs_.set_msp(sp.value);
    if (pc.ok()) {
        regs_.set_thumb((pc.value & 1u) != 0);
        regs_.set_pc(pc.value & ~std::uint32_t{1});
    }
    cycles_ = 0;
}

ExecStatus Cpu::step() {
    // Asynchronous exceptions are checked between instructions.
    if (const int e = highest_pending_exception(); e != 0) {
        clear_pending(static_cast<unsigned>(e));
        return take_exception(static_cast<unsigned>(e), regs_.pc());
    }

    const std::uint32_t pc = regs_.pc();
    const BusResult<std::uint16_t> hw1 = mem_.read_half(pc);
    if (!hw1.ok()) return take_exception(kExcHardFault, pc);

    DecodedInstr d;
    if (is_32bit_thumb(hw1.value)) {
        const BusResult<std::uint16_t> hw2 = mem_.read_half(pc + 2u);
        if (!hw2.ok()) return take_exception(kExcHardFault, pc);
        d = decode_thumb32(hw1.value, hw2.value);
    } else {
        d = decode_thumb16(hw1.value);
    }

    const std::uint32_t seq_pc = pc + d.length;
    regs_.set_pc(seq_pc);  // sequential default; branches override
    const ExecStatus status = execute(d, pc);

    unsigned reg_count = 0;
    switch (d.op) {
        case Mnemonic::LDM: case Mnemonic::STM:
        case Mnemonic::PUSH: case Mnemonic::POP:
            reg_count = popcount16(d.register_list);
            break;
        default:
            break;
    }
    const bool took_branch = regs_.pc() != seq_pc;
    cycles_ += instruction_cycles(d, took_branch, reg_count);

    // Synchronous exceptions raised by the instruction just executed.
    switch (status) {
        case ExecStatus::Svc:
            return take_exception(kExcSVCall, regs_.pc());  // resume after SVC
        case ExecStatus::Undefined:
        case ExecStatus::MemFault:
            return take_exception(kExcHardFault, pc);       // faulting instruction
        default:
            return status;
    }
}

// --- exception model ---------------------------------------------------

std::uint8_t Cpu::exception_priority(unsigned exc) const {
    return exc < priority_.size() ? priority_[exc] : 0u;
}

void Cpu::set_exception_priority(unsigned exc, std::uint8_t raw) {
    if (exc < priority_.size()) priority_[exc] = raw;
}

int Cpu::priority_of(unsigned exc) const {
    switch (exc) {
        case kExcNMI:       return kPriorityNMI;
        case kExcHardFault: return kPriorityHardFault;
        default:            return effective_priority(exception_priority(exc));
    }
}

int Cpu::current_execution_priority() const {
    int prio = 256;  // no exception active
    const unsigned cur = regs_.exception_number();
    if (cur != 0) prio = priority_of(cur);
    if (regs_.primask() && prio > 0) prio = 0;
    return prio;
}

int Cpu::highest_pending_exception() const {
    int best = 0;
    int best_prio = current_execution_priority();
    for (unsigned e = 2; e <= static_cast<unsigned>(kMaxException); ++e) {
        if ((pending_ & (1ull << e)) == 0) continue;
        const int p = priority_of(e);
        if (regs_.primask() && p >= 0) continue;  // PRIMASK masks p >= 0
        if (p < best_prio) {
            best_prio = p;
            best = static_cast<int>(e);
        }
    }
    return best;
}

ExecStatus Cpu::take_exception(unsigned exc, std::uint32_t return_address) {
    // Fault while already at HardFault/NMI priority -> lockup.
    if (exc == kExcHardFault && current_execution_priority() <= kPriorityHardFault) {
        return ExecStatus::Lockup;
    }

    const CpuMode prev_mode = regs_.mode();
    const bool prev_spsel = regs_.control_spsel();
    const std::uint32_t sp0 = regs_.sp();

    const std::uint32_t frameptr = (sp0 - kStackFrameBytes) & ~std::uint32_t{7};
    const bool realigned = (sp0 & 4u) != 0;

    std::uint32_t xpsr = regs_.xpsr();
    xpsr = realigned ? (xpsr | kXpsrStackAlign) : (xpsr & ~kXpsrStackAlign);

    const std::uint32_t frame[kStackFrameWords] = {
        regs_.get(0), regs_.get(1), regs_.get(2), regs_.get(3),
        regs_.get(12), regs_.get(14), return_address, xpsr,
    };
    for (unsigned i = 0; i < kStackFrameWords; ++i) {
        if (mem_.write_word(frameptr + 4u * i, frame[i]) != BusStatus::Ok) {
            return ExecStatus::Lockup;  // stacking error
        }
    }
    regs_.set_sp(frameptr);  // still in the pre-exception mode -> right SP bank

    std::uint32_t exc_return;
    if (prev_mode == CpuMode::Handler)  exc_return = kExcReturnHandlerMSP;
    else if (prev_spsel)                exc_return = kExcReturnThreadPSP;
    else                               exc_return = kExcReturnThreadMSP;
    regs_.set_lr(exc_return);

    regs_.set_exception_number(static_cast<std::uint16_t>(exc));  // -> Handler mode

    const BusResult<std::uint32_t> vec = mem_.read_word(vtor_ + 4u * exc);
    if (!vec.ok()) return ExecStatus::Lockup;
    regs_.set_thumb((vec.value & 1u) != 0);
    regs_.set_pc(vec.value & ~std::uint32_t{1});
    return ExecStatus::ExceptionTaken;
}

ExecStatus Cpu::exception_return(std::uint32_t exc_return) {
    bool return_psp = false;
    switch (exc_return & 0xFu) {
        case 0x1: return_psp = false; break;  // Handler mode, MSP
        case 0x9: return_psp = false; break;  // Thread mode, MSP
        case 0xD: return_psp = true;  break;  // Thread mode, PSP
        default:  return ExecStatus::Lockup;  // invalid EXC_RETURN
    }

    const std::uint32_t frameptr = return_psp ? regs_.psp() : regs_.msp();
    std::uint32_t w[kStackFrameWords];
    for (unsigned i = 0; i < kStackFrameWords; ++i) {
        const BusResult<std::uint32_t> r = mem_.read_word(frameptr + 4u * i);
        if (!r.ok()) return ExecStatus::Lockup;
        w[i] = r.value;
    }

    regs_.set(0, w[kFrameR0]);
    regs_.set(1, w[kFrameR1]);
    regs_.set(2, w[kFrameR2]);
    regs_.set(3, w[kFrameR3]);
    regs_.set(12, w[kFrameR12]);
    regs_.set_lr(w[kFrameLR]);

    std::uint32_t new_sp = frameptr + kStackFrameBytes;
    if ((w[kFrameXPSR] & kXpsrStackAlign) != 0) new_sp += 4u;
    if (return_psp) regs_.set_psp(new_sp); else regs_.set_msp(new_sp);

    regs_.set_xpsr(w[kFrameXPSR]);  // restores N,Z,C,V, T and IPSR (=> mode)

    if (regs_.mode() == CpuMode::Thread) {
        regs_.set_control(regs_.control_npriv(), return_psp);
    }
    regs_.set_pc(w[kFrameReturnAddress] & ~std::uint32_t{1});
    return ExecStatus::Ok;
}

ExecStatus Cpu::execute(const DecodedInstr& d, std::uint32_t instr_pc) {
    switch (d.op) {
        case Mnemonic::UNDEFINED:
        case Mnemonic::UNPREDICTABLE:
        case Mnemonic::UDF:
            return ExecStatus::Undefined;

        case Mnemonic::NOP:
        case Mnemonic::SEV:
        case Mnemonic::YIELD:
        case Mnemonic::DSB:
        case Mnemonic::DMB:
        case Mnemonic::ISB:
            return ExecStatus::Ok;  // barriers are no-ops in a functional model

        case Mnemonic::WFI:
        case Mnemonic::WFE:
            return ExecStatus::WaitingForInterrupt;

        case Mnemonic::BKPT:
            bkpt_imm_ = d.imm;
            return ExecStatus::Breakpoint;

        case Mnemonic::SVC:
            svc_imm_ = d.imm;
            return ExecStatus::Svc;

        case Mnemonic::CPS:
            regs_.set_primask(d.imm != 0);  // CPSID -> mask, CPSIE -> unmask
            return ExecStatus::Ok;

        case Mnemonic::B:
        case Mnemonic::B_cond:
        case Mnemonic::BL:
        case Mnemonic::BX:
        case Mnemonic::BLX_reg:
            return exec_branch(d, instr_pc);

        case Mnemonic::MRS: {
            std::uint32_t v = 0;
            switch (d.imm) {  // SYSm
                case 0: v = regs_.apsr(); break;
                case 1: v = regs_.apsr() | regs_.ipsr(); break;
                case 2: v = regs_.apsr(); break;
                case 3: v = regs_.apsr() | regs_.ipsr(); break;  // EPSR reads 0
                case 5: v = regs_.ipsr(); break;
                case 6: v = 0; break;
                case 7: v = regs_.ipsr(); break;
                case 8: v = regs_.msp(); break;
                case 9: v = regs_.psp(); break;
                case 16: v = regs_.primask() ? 1u : 0u; break;
                case 20:
                    v = (regs_.control_spsel() ? 2u : 0u) |
                        (regs_.control_npriv() ? 1u : 0u);
                    break;
                default: return ExecStatus::Undefined;
            }
            write_reg(d.rd, v);
            return ExecStatus::Ok;
        }

        case Mnemonic::MSR: {
            const std::uint32_t v = read_reg(d.rn, instr_pc);
            switch (d.imm) {  // SYSm
                case 0: case 1: case 2: case 3:
                    regs_.set_apsr(v);  // only N,Z,C,V are writable
                    break;
                case 8: regs_.set_msp(v); break;
                case 9: regs_.set_psp(v); break;
                case 16: regs_.set_primask((v & 1u) != 0); break;
                case 20:
                    regs_.set_control((v & 1u) != 0, (v & 2u) != 0);
                    break;
                default: return ExecStatus::Undefined;
            }
            return ExecStatus::Ok;
        }

        case Mnemonic::LDR_lit:
        case Mnemonic::STR_reg:  case Mnemonic::STRH_reg: case Mnemonic::STRB_reg:
        case Mnemonic::LDRSB_reg: case Mnemonic::LDR_reg: case Mnemonic::LDRH_reg:
        case Mnemonic::LDRB_reg: case Mnemonic::LDRSH_reg:
        case Mnemonic::STR_imm:  case Mnemonic::LDR_imm:  case Mnemonic::STRB_imm:
        case Mnemonic::LDRB_imm: case Mnemonic::STRH_imm: case Mnemonic::LDRH_imm:
        case Mnemonic::STR_imm_sp: case Mnemonic::LDR_imm_sp:
        case Mnemonic::PUSH: case Mnemonic::POP:
        case Mnemonic::STM:  case Mnemonic::LDM:
            return exec_load_store(d, instr_pc);

        default:
            return exec_data(d, instr_pc);
    }
}

ExecStatus Cpu::exec_branch(const DecodedInstr& d, std::uint32_t instr_pc) {
    const std::uint32_t pc4 = instr_pc + 4u;
    const std::uint32_t off = static_cast<std::uint32_t>(d.branch_offset);

    switch (d.op) {
        case Mnemonic::B:
            regs_.set_pc(pc4 + off);
            return ExecStatus::Ok;

        case Mnemonic::B_cond:
            if (regs_.condition_holds(d.cond)) regs_.set_pc(pc4 + off);
            return ExecStatus::Ok;

        case Mnemonic::BL:
            regs_.set_lr((instr_pc + 4u) | 1u);
            regs_.set_pc(pc4 + off);
            return ExecStatus::Ok;

        case Mnemonic::BX: {
            const std::uint32_t target = read_reg(d.rm, instr_pc);
            if (regs_.mode() == CpuMode::Handler && is_exc_return(target)) {
                return exception_return(target);
            }
            regs_.set_thumb((target & 1u) != 0);
            regs_.set_pc(target & ~std::uint32_t{1});
            return ExecStatus::Ok;
        }

        case Mnemonic::BLX_reg: {
            const std::uint32_t target = read_reg(d.rm, instr_pc);
            regs_.set_lr((instr_pc + 2u) | 1u);
            regs_.set_thumb((target & 1u) != 0);
            regs_.set_pc(target & ~std::uint32_t{1});
            return ExecStatus::Ok;
        }

        default:
            return ExecStatus::Undefined;
    }
}

ExecStatus Cpu::exec_data(const DecodedInstr& d, std::uint32_t instr_pc) {
    const bool cin = regs_.c();
    auto rv = [&](unsigned n) { return read_reg(n, instr_pc); };
    auto set_nz = [&](std::uint32_t v) { regs_.set_nz_from(v); };
    auto set_nzcv = [&](const AddResult& r) {
        regs_.set_flags(neg(r.value), r.value == 0, r.carry, r.overflow);
    };

    switch (d.op) {
        case Mnemonic::MOV_imm: {
            write_reg(d.rd, d.imm);
            if (d.setflags) { regs_.set_n(neg(d.imm)); regs_.set_z(d.imm == 0); }
            return ExecStatus::Ok;
        }
        case Mnemonic::MOV_reg: {
            const std::uint32_t r = rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::MOV_reg_hi:
            write_reg(d.rd, rv(d.rm));
            return ExecStatus::Ok;

        case Mnemonic::LSL_imm:
        case Mnemonic::LSR_imm:
        case Mnemonic::ASR_imm: {
            const SRType t = (d.op == Mnemonic::LSL_imm) ? SRType::LSL
                           : (d.op == Mnemonic::LSR_imm) ? SRType::LSR
                                                         : SRType::ASR;
            unsigned amount = d.imm;
            if (t != SRType::LSL && amount == 0) amount = 32;  // DecodeImmShift
            const ShiftResult s = shift_c(rv(d.rm), t, amount, cin);
            write_reg(d.rd, s.value);
            if (d.setflags) { set_nz(s.value); regs_.set_c(s.carry); }
            return ExecStatus::Ok;
        }
        case Mnemonic::LSL_reg:
        case Mnemonic::LSR_reg:
        case Mnemonic::ASR_reg:
        case Mnemonic::ROR_reg: {
            const SRType t = (d.op == Mnemonic::LSL_reg) ? SRType::LSL
                           : (d.op == Mnemonic::LSR_reg) ? SRType::LSR
                           : (d.op == Mnemonic::ASR_reg) ? SRType::ASR
                                                         : SRType::ROR;
            const unsigned amount = rv(d.rm) & 0xFFu;
            const ShiftResult s = shift_c(rv(d.rn), t, amount, cin);
            write_reg(d.rd, s.value);
            if (d.setflags) { set_nz(s.value); regs_.set_c(s.carry); }
            return ExecStatus::Ok;
        }

        case Mnemonic::ADD_reg:
        case Mnemonic::ADD_imm3:
        case Mnemonic::ADD_imm8: {
            const std::uint32_t b = (d.op == Mnemonic::ADD_reg) ? rv(d.rm) : d.imm;
            const AddResult r = add_with_carry(rv(d.rn), b, false);
            write_reg(d.rd, r.value);
            if (d.setflags) set_nzcv(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::SUB_reg:
        case Mnemonic::SUB_imm3:
        case Mnemonic::SUB_imm8: {
            const std::uint32_t b = (d.op == Mnemonic::SUB_reg) ? rv(d.rm) : d.imm;
            const AddResult r = add_with_carry(rv(d.rn), ~b, true);
            write_reg(d.rd, r.value);
            if (d.setflags) set_nzcv(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::ADC_reg: {
            const AddResult r = add_with_carry(rv(d.rn), rv(d.rm), cin);
            write_reg(d.rd, r.value);
            if (d.setflags) set_nzcv(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::SBC_reg: {
            const AddResult r = add_with_carry(rv(d.rn), ~rv(d.rm), cin);
            write_reg(d.rd, r.value);
            if (d.setflags) set_nzcv(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::RSB_imm: {  // Rd = 0 - Rn
            const AddResult r = add_with_carry(~rv(d.rn), 0u, true);
            write_reg(d.rd, r.value);
            if (d.setflags) set_nzcv(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::ADD_reg_hi:
            write_reg(d.rd, rv(d.rn) + rv(d.rm));  // never sets flags
            return ExecStatus::Ok;

        case Mnemonic::CMP_imm:
        case Mnemonic::CMP_reg:
        case Mnemonic::CMP_reg_hi: {
            const std::uint32_t b = (d.op == Mnemonic::CMP_imm) ? d.imm : rv(d.rm);
            set_nzcv(add_with_carry(rv(d.rn), ~b, true));
            return ExecStatus::Ok;
        }
        case Mnemonic::CMN_reg:
            set_nzcv(add_with_carry(rv(d.rn), rv(d.rm), false));
            return ExecStatus::Ok;

        case Mnemonic::AND_reg: {
            const std::uint32_t r = rv(d.rn) & rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::EOR_reg: {
            const std::uint32_t r = rv(d.rn) ^ rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::ORR_reg: {
            const std::uint32_t r = rv(d.rn) | rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::BIC_reg: {
            const std::uint32_t r = rv(d.rn) & ~rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::MVN_reg: {
            const std::uint32_t r = ~rv(d.rm);
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);
            return ExecStatus::Ok;
        }
        case Mnemonic::TST_reg:
            set_nz(rv(d.rn) & rv(d.rm));
            return ExecStatus::Ok;
        case Mnemonic::MUL: {
            const std::uint32_t r = rv(d.rn) * rv(d.rm);  // low 32 bits
            write_reg(d.rd, r);
            if (d.setflags) set_nz(r);  // C, V unchanged on ARMv6-M
            return ExecStatus::Ok;
        }

        case Mnemonic::ADR:
            write_reg(d.rd, ((instr_pc + 4u) & ~std::uint32_t{3}) + d.imm);
            return ExecStatus::Ok;
        case Mnemonic::ADD_SP_imm:
            write_reg(d.rd, regs_.sp() + d.imm);
            return ExecStatus::Ok;
        case Mnemonic::ADD_SP_sp_imm:
            regs_.set_sp(regs_.sp() + d.imm);
            return ExecStatus::Ok;
        case Mnemonic::SUB_SP_imm:
            regs_.set_sp(regs_.sp() - d.imm);
            return ExecStatus::Ok;

        case Mnemonic::SXTB: write_reg(d.rd, sxt8(rv(d.rm)));  return ExecStatus::Ok;
        case Mnemonic::SXTH: write_reg(d.rd, sxt16(rv(d.rm))); return ExecStatus::Ok;
        case Mnemonic::UXTB: write_reg(d.rd, rv(d.rm) & 0xFFu);   return ExecStatus::Ok;
        case Mnemonic::UXTH: write_reg(d.rd, rv(d.rm) & 0xFFFFu); return ExecStatus::Ok;

        case Mnemonic::REV: {
            const std::uint32_t v = rv(d.rm);
            write_reg(d.rd, (v << 24) | ((v & 0xFF00u) << 8) |
                            ((v >> 8) & 0xFF00u) | (v >> 24));
            return ExecStatus::Ok;
        }
        case Mnemonic::REV16: {
            const std::uint32_t v = rv(d.rm);
            write_reg(d.rd, ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu));
            return ExecStatus::Ok;
        }
        case Mnemonic::REVSH: {
            const std::uint32_t v = rv(d.rm);
            write_reg(d.rd, sxt16(((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu)));
            return ExecStatus::Ok;
        }

        default:
            return ExecStatus::Unimplemented;
    }
}

ExecStatus Cpu::exec_load_store(const DecodedInstr& d, std::uint32_t instr_pc) {
    auto rv = [&](unsigned n) { return read_reg(n, instr_pc); };

    switch (d.op) {
        // --- word loads / stores -------------------------------------------
        case Mnemonic::LDR_lit: {
            const std::uint32_t addr = ((instr_pc + 4u) & ~std::uint32_t{3}) + d.imm;
            const BusResult<std::uint32_t> r = mem_.read_word(addr);
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, r.value);
            return ExecStatus::Ok;
        }
        case Mnemonic::LDR_reg:
        case Mnemonic::LDR_imm:
        case Mnemonic::LDR_imm_sp: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::LDR_reg ? rv(d.rm) : d.imm);
            const BusResult<std::uint32_t> r = mem_.read_word(addr);
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, r.value);
            return ExecStatus::Ok;
        }
        case Mnemonic::STR_reg:
        case Mnemonic::STR_imm:
        case Mnemonic::STR_imm_sp: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::STR_reg ? rv(d.rm) : d.imm);
            if (mem_.write_word(addr, rv(d.rt)) != BusStatus::Ok)
                return ExecStatus::MemFault;
            return ExecStatus::Ok;
        }

        // --- halfword ----------------------------------------------------
        case Mnemonic::LDRH_reg:
        case Mnemonic::LDRH_imm: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::LDRH_reg ? rv(d.rm) : d.imm);
            const BusResult<std::uint16_t> r = mem_.read_half(addr);
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, r.value);  // zero-extended
            return ExecStatus::Ok;
        }
        case Mnemonic::LDRSH_reg: {
            const BusResult<std::uint16_t> r = mem_.read_half(rv(d.rn) + rv(d.rm));
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, sxt16(r.value));
            return ExecStatus::Ok;
        }
        case Mnemonic::STRH_reg:
        case Mnemonic::STRH_imm: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::STRH_reg ? rv(d.rm) : d.imm);
            if (mem_.write_half(addr, static_cast<std::uint16_t>(rv(d.rt))) != BusStatus::Ok)
                return ExecStatus::MemFault;
            return ExecStatus::Ok;
        }

        // --- byte ------------------------------------------------------
        case Mnemonic::LDRB_reg:
        case Mnemonic::LDRB_imm: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::LDRB_reg ? rv(d.rm) : d.imm);
            const BusResult<std::uint8_t> r = mem_.read_byte(addr);
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, r.value);  // zero-extended
            return ExecStatus::Ok;
        }
        case Mnemonic::LDRSB_reg: {
            const BusResult<std::uint8_t> r = mem_.read_byte(rv(d.rn) + rv(d.rm));
            if (!r.ok()) return ExecStatus::MemFault;
            write_reg(d.rt, sxt8(r.value));
            return ExecStatus::Ok;
        }
        case Mnemonic::STRB_reg:
        case Mnemonic::STRB_imm: {
            const std::uint32_t addr =
                rv(d.rn) + (d.op == Mnemonic::STRB_reg ? rv(d.rm) : d.imm);
            if (mem_.write_byte(addr, static_cast<std::uint8_t>(rv(d.rt))) != BusStatus::Ok)
                return ExecStatus::MemFault;
            return ExecStatus::Ok;
        }

        // --- stack push / pop ------------------------------------------
        case Mnemonic::PUSH: {
            const unsigned n = popcount16(d.register_list);
            if (n == 0) return ExecStatus::Undefined;  // UNPREDICTABLE
            const std::uint32_t sp = regs_.sp();
            std::uint32_t addr = sp - 4u * n;
            for (unsigned i = 0; i < 16; ++i) {
                if ((d.register_list & (1u << i)) == 0) continue;
                if (mem_.write_word(addr, read_reg(i, instr_pc)) != BusStatus::Ok)
                    return ExecStatus::MemFault;
                addr += 4u;
            }
            regs_.set_sp(sp - 4u * n);
            return ExecStatus::Ok;
        }
        case Mnemonic::POP: {
            const unsigned n = popcount16(d.register_list);
            if (n == 0) return ExecStatus::Undefined;
            const std::uint32_t sp = regs_.sp();
            std::uint32_t addr = sp;
            for (unsigned i = 0; i < 15; ++i) {  // r0..r14
                if ((d.register_list & (1u << i)) == 0) continue;
                const BusResult<std::uint32_t> r = mem_.read_word(addr);
                if (!r.ok()) return ExecStatus::MemFault;
                regs_.set(i, r.value);
                addr += 4u;
            }
            if ((d.register_list & (1u << 15)) != 0) {  // POP {..., PC}
                const BusResult<std::uint32_t> r = mem_.read_word(addr);
                if (!r.ok()) return ExecStatus::MemFault;
                regs_.set_sp(sp + 4u * n);
                if (regs_.mode() == CpuMode::Handler && is_exc_return(r.value)) {
                    return exception_return(r.value);
                }
                regs_.set_thumb((r.value & 1u) != 0);      // LoadWritePC == BXWritePC
                regs_.set_pc(r.value & ~std::uint32_t{1});
                return ExecStatus::Ok;
            }
            regs_.set_sp(sp + 4u * n);
            return ExecStatus::Ok;
        }

        // --- load / store multiple (16-bit: r0..r7 only) -----------------
        case Mnemonic::STM: {
            const unsigned n = popcount16(d.register_list);
            if (n == 0) return ExecStatus::Undefined;
            const std::uint32_t base = rv(d.rn);
            std::uint32_t addr = base;
            for (unsigned i = 0; i < 8; ++i) {
                if ((d.register_list & (1u << i)) == 0) continue;
                if (mem_.write_word(addr, regs_.get(i)) != BusStatus::Ok)
                    return ExecStatus::MemFault;
                addr += 4u;
            }
            if (d.wback) regs_.set(d.rn, base + 4u * n);
            return ExecStatus::Ok;
        }
        case Mnemonic::LDM: {
            const unsigned n = popcount16(d.register_list);
            if (n == 0) return ExecStatus::Undefined;
            const std::uint32_t base = rv(d.rn);
            std::uint32_t addr = base;
            for (unsigned i = 0; i < 8; ++i) {
                if ((d.register_list & (1u << i)) == 0) continue;
                const BusResult<std::uint32_t> r = mem_.read_word(addr);
                if (!r.ok()) return ExecStatus::MemFault;
                regs_.set(i, r.value);
                addr += 4u;
            }
            if (d.wback) regs_.set(d.rn, base + 4u * n);
            return ExecStatus::Ok;
        }

        default:
            return ExecStatus::Unimplemented;
    }
}

}  // namespace rp2040
