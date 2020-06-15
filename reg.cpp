#include "st8.hpp"
#include <fpro.h>
#include <diskio.hpp>
#include <entry.hpp>
#include <ieee.h>

//--------------------------------------------------------------------------
static const char *register_names[] =
{
  "a", "x", "y", "cc", "sp",
  "xl", "xh", "yl", "yh",
  "ds", "cs",
};

//--------------------------------------------------------------------------
static uchar retcode0[] = { 0x80 }; // iret  80
static uchar retcode1[] = { 0x81 }; // ret   81
static uchar retcode2[] = { 0x87 }; // retf  87
static bytes_t retcodes[] =
{
 { sizeof(retcode0), retcode0 },
 { sizeof(retcode1), retcode1 },
 { sizeof(retcode2), retcode2 },
 { 0, NULL }
};

static const uint16 cos_bad_insns[] = { ST8_int, 0 }; //casmst8 doesn't support int

static asm_t cosmic =
{
  ASH_HEXF3 |    // 0x1234
  ASD_DECF0 |    // 1234
  ASB_BINF2 |    // %1010
  ASO_OCTF3 |    // @1234
  AS_NOXRF |     // Disable xrefs during the output file generation
  AS_ONEDUP |    // one array definition per line
  AS_COLON,      // data labels with colon
  ASM_COSMIC,	 // uflag,
  "Cosmic CASTM8",
  0,
  NULL,   // header lines
  "org",        // org
  "end",        // end

  ";",          // comment string
  '\"',         // string delimiter
  '\'',         // char delimiter
  "'\"",        // special symbols in char and string constants

  // Data representation (db,dw,...):
  "dc.b",       //   // Data representation (db,dw,...):
  "dc.b",       // byte directive
  "dc.w",       // word directive
  "dc.l",       // double words
  NULL,         // qwords
  NULL,         // oword  (16 bytes)
  "dc.flt",     // float  (4 bytes)	// Fake directive, but float available
  "dc.dbl",     // double (8 bytes)
  NULL,         // tbyte  (10/12 bytes)  long double;
  NULL,         // packed decimal real
  "dcb.#s(b,w,l) #d, #v", // arrays (#h,#d,#v,#s(...)
  "ds.b %s",    // uninited arrays
  "equ",        // equ
  NULL,         // 'seg' prefix (example: push seg seg001)
  "*",          // current IP (instruction pointer)

  NULL,         // func_header
  NULL,         // func_footer
  
  "xdef",     // "public" name keyword
  "wdef",         // "weak"   name keyword
  "xref",     // "extrn"  name keyword
				// .extern directive requires an explicit object size
  NULL,         // "comm" (communal variable)
  
  NULL,         // get_type_name
  "align",         // "align" keyword
  '{', '}',     // lbrace, rbrace
  "%",         // mod
  "&",        // and
  "|",         // or
  "^",        // xor
  "~",         // not
  "<<",        // shl
  ">>",        // shr
  NULL,         // sizeof
  AS2_BRACE,
  NULL, //cmnt2
  "low(%s)", //low8
  "high(%s)", //high8
  NULL, //low16
  NULL, //high16
  "include \"%s\"", //include
  NULL, //verbose struct
  NULL, //rva
  NULL, //yword
};

//-----------------------------------------------------------------------
//      STMicroelectronics - Assembler - rel. 4.10
//      We support Motorola format
//-----------------------------------------------------------------------
static const char *st8_header[] =
{
	"STM8",
	"",
	NULL
};

static asm_t stasm =
{
  ASH_HEXF4 |    // $1234
  ASD_DECF0 |    // 1234
  ASB_BINF2 |    // %1010
  ASO_OCTF6 |    // ~1234
  AS_NOXRF |     // Disable xrefs during the output file generation
  AS_ONEDUP,    // one array definition per line
  0,
  "STMicroelectronics - Assembler",
  0,
  st8_header,   // header lines
  "org",        // org
  "end",        // end

  ";",          // comment string
  '\"',         // string delimiter
  '\'',         // char delimiter
  "'\"",        // special symbols in char and string constants

  // Data representation (db,dw,...):
  "dc.b",       // ascii string directive
  "dc.b",       // byte directive
  "dc.w",       // word directive
  "dc.l",       // double words
  NULL,         // qwords
  NULL,         // oword  (16 bytes)
  "dc.flt",     // float  (4 bytes)	// Fake directive, but float available
  "dc.dbl",     // double (8 bytes)
  NULL,         // tbyte  (10/12 bytes)
  NULL,         // packed decimal real
  "skip#s( )#d, #v", // arrays (#h,#d,#v,#s(...)  ONLY BYTE ARRAYS!!!
  "ds.b %s",    // uninited arrays
  "equ",        // equ
  NULL,         // 'seg' prefix (example: push seg seg001)
  "*",          // current IP (instruction pointer)
  NULL,         // func_header
  NULL,         // func_footer
  "public",     // "public" name keyword
  NULL,         // "weak"   name keyword
  "extern",     // "extrn"  name keyword
				// .extern directive requires an explicit object size
  NULL,         // "comm" (communal variable)
  NULL,         // get_type_name
  NULL,         // "align" keyword
  '{', '}',     // lbrace, rbrace
  NULL,         // mod
  "and",        // and
  "or",         // or
  "xor",        // xor
  NULL,         // not
  "shl",        // shl
  "shr",        // shr
  NULL,         // sizeof
  AS2_BRACE,
};

static asm_t *asms[] = { &stasm, &cosmic, NULL };

//--------------------------------------------------------------------------
ea_t memstart;
static netnode helper;
qstring device;
static ioports_t ports;
static const char cfgname[] = "stm8.cfg";

#include <iocommon.cpp>

//
static void load_symbols(void)
{
	ports.clear();
	read_ioports(&ports, &device, cfgname);
 }
//
//----------------------------------------------------------------------
const ioport_t *find_sym(ea_t address)
{
	const ioport_t *port = find_ioport(ports, address);
	return port;
}

//----------------------------------------------------------------------
static void create_words(void)
{
	for (size_t i = 0; i < ports.size(); i++)
	{
		ea_t ea = ports[i].address;
		if (is_tail(get_flags(ea)))
			del_items(ea, DELIT_SIMPLE);
		create_word(ea, 2);
	}
}

//--------------------------------------------------------------------------
const char * idaapi set_idp_options(const char *keyword, int /*value_type*/, const void * /*value*/)
{
	if (keyword != NULL) 
		return IDPOPT_BADKEY;
// 	char cfgfile[QMAXFILE];
// 	get_cfg_filename(cfgfile, sizeof(cfgfile));
	if (choose_ioport_device(&device, cfgname))
		load_symbols();// set_device_name(device.c_str(), IORESP_PORT | IORESP_INT);
	return IDPOPT_OK;
}

static ssize_t idaapi idb_callback(void *, int code, va_list /*va*/)
{
	switch (code)
	{
	case idb_event::closebase:
	case idb_event::savebase:
		helper.supset(0, device.c_str());
		break;
	}
	return 0;
}
//--------------------------------------------------------------------------
static ssize_t idaapi notify(void *, int msgid, va_list va)
{
	int retcode = 0;

	switch (msgid)
	{
	case processor_t::ev_init:
		hook_to_notification_point(HT_IDB, idb_callback);
		helper.create("$ stm8");
		helper.supstr(&device, 0);
		inf_set_be(true);
		break;

	case processor_t::ev_term:
		ports.clear();
		unhook_from_notification_point(HT_IDB, idb_callback);
		break;

	case processor_t::ev_newfile:  // new file loaded
		{
// 			char cfgfile[QMAXFILE];
// 			get_cfg_filename(cfgfile, sizeof(cfgfile));

			if (choose_ioport_device(&device, cfgname))
				set_device_name(device.c_str(), IORESP_ALL);
			create_words();
		}
		break;

	case processor_t::ev_oldfile:  // old file loaded
		{
			char buf[MAXSTR];
			if (helper.supval(-1, buf, sizeof(buf)) > 0)
				set_device_name(buf, IORESP_NONE);
		}
		break;

	case processor_t::ev_is_jump_func:
		{
			const func_t *pfn = va_arg(va, const func_t *);
			ea_t *jump_target = va_arg(va, ea_t *);
			return is_jump_func(pfn, jump_target);
		}

	case processor_t::ev_is_ret_insn:
		{
			const insn_t *insn = va_arg(va, insn_t *);
			const struct opcode_t &opinfo = get_opcode_info(get_byte(insn->ea));
			if (opinfo.itype == ST8_iret
				|| opinfo.itype == ST8_ret
				|| opinfo.itype == ST8_retf)
				retcode = 1;
			else
				retcode = -1;
		}
		break;

	case processor_t::ev_is_sane_insn:
		{
			const insn_t &insn = *va_arg(va, insn_t *);
			int no_crefs = va_arg(va, int);
			retcode = is_sane_insn(insn, no_crefs) == 1 ? 1 : -1;
		}
		break;

	case processor_t::ev_is_switch:
		{
			switch_info_t *si = va_arg(va, switch_info_t *);
			const insn_t *insn = va_arg(va, const insn_t *);
			return stm8_is_switch(si, *insn) ? 1 : 0;
		}

	case processor_t::ev_gen_stkvar_def:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			const ::member_t * memt = va_arg(va, const ::member_t *);
			sval_t v = va_arg(va, sval_t);

			stm8_gen_stkvar_def(*ctx, memt, v);
			return 1;
		}

	case processor_t::ev_set_idp_options:
		{
			const char *keyword = va_arg(va, const char *);
			int value_type = va_arg(va, int);
			const char *value = va_arg(va, const char *);
			const char *retstr = set_idp_options(keyword, value_type, value);
			if (retstr == IDPOPT_OK)
				return 1;
			const char **errmsg = va_arg(va, const char **);
			if (errmsg != NULL)
				*errmsg = retstr;
			return -1;
		}

	case processor_t::ev_is_align_insn:
		{
			ea_t ea = va_arg(va, ea_t);
			return stm8_is_align_insn(ea);
		}

	case processor_t::ev_may_be_func:
		// can a function start here?
		// arg: none, the instruction is in 'cmd'
		// returns: probability 0..100
		// 'cmd' structure is filled upon the entrace
		// the idp module is allowed to modify 'cmd'
		return may_be_func();

	case processor_t::ev_get_autocmt:
		{
			qstring *buf = va_arg(va, qstring *);
			const insn_t *insn = va_arg(va, const insn_t*);
			*buf = insn_auto_cmts[insn->itype];
			return 1;
		}
		break;

	case processor_t::ev_out_header:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			stm8_header(*ctx);
			return 1;
		}

	case processor_t::ev_out_footer:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			stm8_footer(*ctx);
			return 1;
		}

	case processor_t::ev_out_segstart:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			segment_t *seg = va_arg(va, segment_t *);
			stm8_segstart(*ctx, seg);
			return 1;
		}

	case processor_t::ev_out_segend:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			segment_t *seg = va_arg(va, segment_t *);
			stm8_segend(*ctx, seg);
			return 1;
		}

	case processor_t::ev_ana_insn:
		{
			insn_t *out = va_arg(va, insn_t *);
			return ana(out);
		}

	case processor_t::ev_emu_insn:
		{
			const insn_t *insn = va_arg(va, const insn_t *);
			return emu(*insn) ? 1 : -1;
		}

	case processor_t::ev_out_insn:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			out_insn(*ctx);
			return 1;
		}

	case processor_t::ev_out_operand:
		{
			outctx_t *ctx = va_arg(va, outctx_t *);
			const op_t *op = va_arg(va, const op_t *);
			return out_opnd(*ctx, *op) ? 1 : -1;
		}

	default:
		break;
	}

	return retcode;
}

//-----------------------------------------------------------------------
#define FAMILY "SGS-Thomson STM8:"
static const char *const shnames[] = { "stm8", NULL };
static const char *const lnames[] = {
  FAMILY"SGS-Thomson STM8",
  NULL
};

//-----------------------------------------------------------------------
//      Processor Definition
//-----------------------------------------------------------------------
processor_t LPH =
{
	IDP_INTERFACE_VERSION,      // version
	0x8000,                     // id
	PRN_HEX | PR_RNAMESOK,		// flag
								// flag2
	PR2_REALCVT					// the module has 'realcvt' event implementation
  | PR2_IDP_OPTS,				// the module has processor-specific configuration options
	8,                          // 8 bits in a byte for code segments
	8,                          // 8 bits in a byte for other segments

	shnames,
	lnames,

	asms,

	notify,						//Event notification handler

	register_names,				// Register names  
	qnumber(register_names),	// Number of registers

	ds,							// first segreg
	cs,							// last  segreg
	2,							// size of a segment register
	cs, 						// number of CS register
	ds,							// number of DS register

	NULL,						// codestart 当一个新文件加载时, 用来识别代码开始位置的序列
	retcodes,

	ST8_null,					// < icode of the first instruction
	ST8_last,					// < icode of the last instruction + 1
	Instructions,

	0,                   // int tbyte_size;  -- doesn't exist

	{ 0, 7, 15, 0 },     // char real_width[4];
						 // number of symbols after decimal point
						 // 2byte float (0-does not exist)
						 // normal float
						 // normal double
						 // long double
	ST8_ret,             // Icode of return instruction. It is ok to give any of possible return instructions

};
