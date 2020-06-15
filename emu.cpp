#include "st8.hpp"
#include <frame.hpp>
#include <jumptable.hpp>

static int flow;
//------------------------------------------------------------------------
static void process_immediate_number(const insn_t &insn, int n)
{
	set_immd(insn.ea);
	if (is_defarg(insn.flags, n)) return;
	switch (insn.itype)
	{
	case ST8_bres:
	case ST8_bset:
	case ST8_btjf:
	case ST8_btjt:
		op_dec(insn.ea, n);
		break;
	}
}

//----------------------------------------------------------------------
inline bool issp(int x)
{
	return x == SP;
}
//----------------------------------------------------------------------
int idaapi is_sp_based(const insn_t &, const op_t &x)
{
	return OP_SP_ADD
		| ((x.type == o_displ || x.type == o_phrase) && issp(x.phrase)
			? OP_SP_BASED
			: OP_FP_BASED);
}
//----------------------------------------------------------------------
static void add_stkpnt(const insn_t &insn, sval_t value)
{
	func_t *pfn = get_func(insn.ea);
	if (pfn == NULL)
		return;

	add_auto_stkpnt(pfn, insn.ea + insn.size, value);
}

//----------------------------------------------------------------------
static bool get_op_value(
	const insn_t &_insn,
	const op_t &x,
	uval_t *value,
	ea_t *base_addr = NULL)
{
	if (base_addr != NULL)
		*base_addr = 0;

	if (x.type == o_imm)
	{
		*value = x.value;
		return true;
	}

	uint16 reg = x.reg;

	bool ok = false;
	insn_t insn;
	ea_t next_ea = _insn.ea;
	while ((!has_xref(get_flags(next_ea)) || get_first_cref_to(next_ea) == BADADDR)
		&& decode_prev_insn(&insn, next_ea) != BADADDR)
	{
		if ((insn.itype == ST8_ld || insn.itype == ST8_ldw)
			&& insn.Op2.type == o_imm
			&& insn.Op1.type == o_reg
			&& insn.Op1.reg == reg)
		{
			*value = insn.Op2.value;
			ok = true;
			break;
		}

		next_ea = insn.ea;
	}

	return ok;
}
//----------------------------------------------------------------------
static void trace_sp(const insn_t &insn)
{
/*
	// @sp++
	if (insn.Op1.type == o_phrase
		&& issp(insn.Op1.reg)
		&& insn.Op1.phtype == ph_post_inc)
	{
		ssize_t size = get_dtype_size(insn.Op2.dtype);
		if (insn.Op2.type == o_reglist)
			size *= insn.Op2.nregs;
		add_stkpnt(insn, size);
		return;
	}

	// @--sp
	if (insn.Op2.type == o_phrase
		&& issp(insn.Op2.reg)
		&& insn.Op2.phtype == ph_pre_dec)
	{
		ssize_t size = get_dtype_size(insn.Op1.dtype);
		if (insn.Op1.type == o_reglist)
			size *= insn.Op1.nregs;
		add_stkpnt(insn, -size);
		return;
	}
*/

	uval_t v;
	switch (insn.itype)
	{

	case ST8_add:
	case ST8_addw:
		if (!issp(insn.Op1.reg))
			break;
		if (get_op_value(insn, insn.Op2, &v))
			add_stkpnt(insn, v);
		break;
	case ST8_sub:
	case ST8_subw:
		if (!issp(insn.Op1.reg))
			break;
		if (get_op_value(insn, insn.Op2, &v))
			add_stkpnt(insn, 0 - v);
		break;
	case ST8_push:
		add_stkpnt(insn, 0 - get_dtype_size(insn.Op1.dtype));
		break;
	case ST8_pushw:
		add_stkpnt(insn, 0 - get_dtype_size(insn.Op1.dtype));
		break;
	case ST8_pop:
		add_stkpnt(insn, get_dtype_size(insn.Op1.dtype));
		break;
	case ST8_popw:
		add_stkpnt(insn, get_dtype_size(insn.Op1.dtype));
		break;
	}
}

//----------------------------------------------------------------------
ea_t calc_mem(const insn_t &insn, ea_t ea)
{
	return to_ea(insn.cs, ea);
}
//----------------------------------------------------------------------
static void add_code_xref(const insn_t &insn, const op_t &x, ea_t ea)
{
	cref_t ftype = fl_JN;
	if (has_insn_feature(insn.itype, CF_CALL))
	{
		if (!func_does_return(ea))
			flow = false;
		ftype = fl_CN;
	}
	insn.add_cref(ea, x.offb, ftype);
}
//----------------------------------------------------------------------
static void process_operand(const insn_t &insn, const op_t &x, bool isload)
{
	ea_t ea = insn.ea;
	flags_t F = get_flags(insn.ea);
	switch (x.type)
	{
	case o_reg:
	case o_phrase:
		break;

	case o_imm:
		if (!isload) interr(insn, "emu");
		process_immediate_number(insn, x.n);
		if (op_adds_xrefs(F, x.n))
			insn.add_off_drefs(x, dr_O, OOFS_IFSIGN | OOFW_IMM);
		break;

	case o_mem:
		if (!is_forced_operand(insn.ea, x.n))
		{
			ea = calc_mem(insn, x.addr);
			insn.create_op_data(ea, x.offb, x.dtype);
			dref_t dref = isload || (insn.auxpref & aux_indir) ? dr_R : dr_W;
			insn.add_dref(ea, x.offb, dref);
		}
		break;

	case o_near:
		add_code_xref(insn, x, calc_mem(insn, x.addr));		
		break;

	case o_displ:
		process_immediate_number(insn, x.n);
		if (is_off(insn.flags, x.n) && !is_forced_operand(insn.ea, x.n))
			insn.add_off_drefs(x, isload ? dr_R : dr_W, OOF_ADDR);
		break;

	default:
		interr(insn, "emu");
	}
}


//----------------------------------------------------------------------
int idaapi emu(const insn_t &insn)
{
	uint32 Feature = insn.get_canon_feature();

	flow = ((Feature & CF_STOP) == 0);

	if (Feature & CF_USE1) 
		process_operand(insn, insn.Op1, true);
	if (Feature & CF_USE2) 
		process_operand(insn, insn.Op2, true);
	if (Feature & CF_USE3) 
		process_operand(insn, insn.Op3, true);
	if (Feature & CF_CHG1) 
		process_operand(insn, insn.Op1, false);
	if (Feature & CF_CHG2)
		process_operand(insn, insn.Op2, false);
	if (Feature & CF_CHG3)
		process_operand(insn, insn.Op3, false);

	//
	// 确定是否应该执行下一条指令, 关键, 这里处理不对, 一次只会分析一条指令了
	//
	if (segtype(insn.ea) == SEG_XTRN) 
		flow = 0;
	if (flow) 
		insn.add_cref(insn.ea + insn.size, 0, fl_F);

	//
	// Handle SP modifications
// 	if (may_trace_sp())
// 	{
// 		if (!flow)
// 			recalc_spd(insn.ea);     // recalculate SP register for the next insn
// 		else
// 			trace_sp(insn);
// 	}

	return 1;
}

//----------------------------------------------------------------------
int is_jump_func(const func_t * /*pfn*/, ea_t *jump_target)
{
	*jump_target = BADADDR;
	return 1; // means "no"
}

//----------------------------------------------------------------------
int may_be_func(void)           // can a function start here?
								// arg: none, the instruction is in 'cmd'
								// returns: probability 0..100
								// 'cmd' structure is filled upon the entrace
								// the idp module is allowed to modify 'cmd'
{
	//  if ( insn.itype == H8_push && isbp(insn.Op1.reg) ) return 100;  // push.l er6
	return 0;
}

//----------------------------------------------------------------------
int is_sane_insn(const insn_t &insn, int /*nocrefs*/)
{
	if (insn.itype == ST8_nop)
	{
		for (int i = 0; i < 8; i++)
			if (get_word(insn.ea - i * 2) != 0) return 1;
		return 0; // too many nops in a row
	}
	return 1;
}

//----------------------------------------------------------------------
// Known STM8 switch idioms:
//  Switch statements are straightforward:
//     cp      a, #$E        ; Arithmetic Compare
//     jrnc    out_of_bounds ; Jump if C = 0
//     clrw    x             ; x = 0
//     ld      xl, a         ; x |= a & 0xff
//     sllw    x             ; index = x * sizeof(u16)
//     ldw     x, ($F693,x)  ; x = *(0xf693 + index)
//     jp      (x)           ; Absolute Jump
//   out_of_bounds:
//     jra     return        ; Jump relative always
//  Indirect calls are less code but harder to resolve,
//  as they lookup the target address from memory that is
//  modified during runtime.
//     ld      a, byte_1E0   ; a = index
//     clrw    y             ; y = 0
//     ld      yl, a         ; y |= a & 0xff
//     sllw    y             ; y *= sizeof(u16)
//     ldw     y, (7,y)      ; t = *(7 + y) // data at 7 is changed at runtime
//     call    (y)           ; Call subroutine

class stm8_jump_pattern_t : public jump_pattern_t
{
	enum { rA, rC };
public:
	stm8_jump_pattern_t(switch_info_t *si)
		: jump_pattern_t(si, depends, rC)
		, indirect_register(-1)
		, index_register(-1)
	{
	}

	static char const roots[];
	static char const depends[][4];

	int indirect_register;
	int index_register;

	bool jpi0()
	{
		bool valid = insn.itype == ST8_jp;
		if (valid)
			indirect_register = insn.Op1.reg;
		return valid;
	}
	bool jpi1()
	{
		bool valid = insn.itype == ST8_ldw && insn.Op1.reg == indirect_register;
		if (valid)
			si->jumps = insn.Op2.addr;
		return valid;
	}
	bool jpi2()
	{
		return insn.itype == ST8_sllw && insn.Op1.reg == indirect_register;
	}
	bool jpi3()
	{
		uint16 dst_reg = insn.Op1.reg;
		if ((indirect_register == regnum_t::X) && (dst_reg != regnum_t::XL))
			return false;
		else if ((indirect_register == regnum_t::Y) && (dst_reg != regnum_t::YL))
			return false;

		index_register = insn.Op2.reg;

		return insn.itype == ST8_ld;
	}
	bool jpi4()
	{
		return insn.itype == ST8_clrw && insn.Op1.reg == indirect_register;
	}
	bool jpi6()
	{
		bool valid = insn.itype == ST8_cp && insn.Op1.reg == index_register;
		if (valid)
			si->ncases = insn.Op2.value;
		return valid;
	}
};

char const stm8_jump_pattern_t::roots[] = { 1 };
char const stm8_jump_pattern_t::depends[][4] =
{
	{ 1 },    // jp      (x)
	{ 2 },    // ldw     x, ($F693,x)
	{ 3 },    // sllw    x
	{ 4, 6 }, // ld      xl, a
	{ 0 },    // clrw    x
	{ 0 },    // jrnc    out_of_bounds
	{ 0 }     // cp      a, #$E
};

bool idaapi stm8_is_switch(switch_info_t *si, const insn_t &insn)
{
	return stm8_jump_pattern_t(si).match(insn);
}

//----------------------------------------------------------------------
int idaapi stm8_is_align_insn(ea_t ea)
{
	insn_t insn;
	if (decode_insn(&insn, ea) < 1)
		return 0;
	switch (insn.itype)
	{
	case ST8_nop:
		break;
	default:
		return 0;
	}
	return insn.size;
}

