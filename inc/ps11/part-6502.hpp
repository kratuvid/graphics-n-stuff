#pragma once

#include "utility.hpp"

class Configuration
{
public:
	using value_t = std::variant<std::string, long>;
	using map_t = std::unordered_map<std::string, value_t>;

private:
	map_t store;

public:
	Configuration(std::string_view path)
	{
		std::ifstream file(path.data());
		iassert(file.is_open(), "Cannot open configuration file {}", path);

		const std::regex reg_empty(R"(\s*)"), reg_comment(R"(\s*;.*)"), reg_data(R"(\s*(.+?)\s*?=\s*(.+?)\s*)");
		const std::regex reg_key(R"(([sl])\s*?:\s*(.+))");

		std::string line;
		while (std::getline(file, line))
		{
			std::smatch match;
			if (std::regex_match(line, match, reg_empty))
				continue;
			else if (std::regex_match(line, match, reg_comment))
				continue;
			else if (std::regex_match(line, match, reg_data))
			{
				std::string only_key = match[1].str();
				std::string value = match[2].str();
				if (std::regex_match(only_key, match, reg_key))
				{
					char value_format = match[1].str()[0];
					auto key = match[2].str();

					switch (value_format)
					{
					case 's':
						store[key] = value;
						break;
					case 'l':
						char* str_end = nullptr;
						long value_conv = strtol(value.c_str(), &str_end, 10);
						iassert(str_end != nullptr, "String to long conversion failed at line '{}' in file {}", line, path);
						store[key] = value_conv;
						break;
					}
				}
				else iassert(false, "Bad key format '{}' in file {}", line, path);
			}
			else iassert(false, "Invalid line '{}' in file {}", line, path);
		}
	}

	template<typename T>
	T const& get(const std::string& key) const
	{
		return std::get<T>(store.at(key));
	}
};

class PS11
{
private:
	inline static const std::string_view opname_str[] {
		/* NOP */
		"NOP",

		/* FLAGS */
		"CLC", "CLD", "CLI", "CLV",
		"SEC", "SED", "SEI",

		/* CONTROL FLOW: BRANCH */
		"BCC", "BNE", "BPL", "BVC",
		"BCS", "BEQ", "BMI", "BVS",

		/* ARITHMETIC: INC/DEC */
		"DEC", "DEX", "DEY",
		"INC", "INX", "INY",

		/* TRANSFER */
		"TAX", "TAY", 
		"TSX",
		"TXA", "TXS",
		"TYA",

		/* STACK */
		"PHA", "PHP",
		"PLA", "PLP",

		/* LOAD/STORE */
		"LDA", "LDX", "LDY",
		"STA", "STX", "STY",

		/* SHIFT */
		"ASL", "LSR",
		"ROL", "ROR",

		/* LOGIC */
		"AND", "BIT",
		"EOR", "ORA",

		/* ARITHMETIC */
		"ADC", "SBC",
		"CMP", "CPX", "CPY",

		/* CONTROL FLOW */
		"BRK",
		"JMP", "JSR",
		"RTI", "RTS",
	};

	enum class Opname {
		/* NOP */
		nop,

		/* Flags */
		clc, cld, cli, clv,
		sec, sed, sei,

		/* Control Flow: Branch */
		bcc, bne, bpl, bvc,
		bcs, beq, bmi, bvs,

		/* Arithmetic: Inc/Dec */
		dec, dex, dey,
		inc, inx, iny,

		/* Transfer */
		tax, tay, 
		tsx,
		txa, txs,
		tya,

		/* Stack */
		pha, php,
		pla, plp,

		/* Load/Store */
		lda, ldx, ldy,
		sta, stx, sty,

		/* Shift */
		asl, lsr,
		rol, ror,

		/* Logic */
		andd, bit,
		eor, ora,

		/* Arithmetic */
		adc, sbc,
		cmp, cpx, cpy,

		/* Control Flow */
		brk,
		jmp, jsr,
		rti, rts,
	};

	enum class Addressing {
		none, // includes implied + accumulator

		immediate,

		absolute,
		x_indexed_absolute,
		y_indexed_absolute,

		absolute_indirect,

		zero_page,
		x_indexed_zero_page,
		y_indexed_zero_page,
		x_indexed_zero_page_indirect,
		zero_page_indirect_y_indexed,

		relative,
	};

	inline static const std::unordered_map<uint8_t /*opcode*/, std::tuple<Opname, Addressing, uint8_t /*ideal cycles*/>> opcode_table {
		/* NOP */
		{0xea, {Opname::nop, Addressing::none, 2}},

		/* Flags */
		{0x18, {Opname::clc, Addressing::none, 2}},
		{0xd8, {Opname::cld, Addressing::none, 2}},
		{0x58, {Opname::cli, Addressing::none, 2}},
		{0xb8, {Opname::clv, Addressing::none, 2}},
		{0x38, {Opname::sec, Addressing::none, 2}},
		{0xf8, {Opname::sed, Addressing::none, 2}},
		{0x78, {Opname::sei, Addressing::none, 2}},

		/* Control Flow: Branch */
		{0x90, {Opname::bcc, Addressing::relative, 2}},
		{0xd0, {Opname::bne, Addressing::relative, 2}},
		{0x10, {Opname::bpl, Addressing::relative, 2}},
		{0x50, {Opname::bvc, Addressing::relative, 2}},
		{0xb0, {Opname::bcs, Addressing::relative, 2}},
		{0xf0, {Opname::beq, Addressing::relative, 2}},
		{0x30, {Opname::bmi, Addressing::relative, 2}},
		{0x70, {Opname::bvs, Addressing::relative, 2}},

		/* Arithmetic: Inc/Dec */
		// dec
		{0xce, {Opname::dec, Addressing::absolute, 6}},
		{0xde, {Opname::dec, Addressing::x_indexed_absolute, 7}},
		{0xc6, {Opname::dec, Addressing::zero_page, 5}},
		{0xd6, {Opname::dec, Addressing::x_indexed_zero_page, 6}},
		// dex
		{0xca, {Opname::dex, Addressing::none, 2}},
		// dey
		{0x88, {Opname::dey, Addressing::none, 2}},
		// inc
		{0xee, {Opname::inc, Addressing::absolute, 6}},
		{0xfe, {Opname::inc, Addressing::x_indexed_absolute, 7}},
		{0xe6, {Opname::inc, Addressing::zero_page, 5}},
		{0xf6, {Opname::inc, Addressing::x_indexed_zero_page, 6}},
		// inx
		{0xe8, {Opname::inx, Addressing::none, 2}},
		// iny
		{0xc8, {Opname::iny, Addressing::none, 2}},

		/* Transfer */
		{0xaa, {Opname::tax, Addressing::none, 2}},
		{0xa8, {Opname::tay, Addressing::none, 2}},
		{0xba, {Opname::tsx, Addressing::none, 2}},
		{0x8a, {Opname::txa, Addressing::none, 2}},
		{0x9a, {Opname::txs, Addressing::none, 2}},
		{0x98, {Opname::tya, Addressing::none, 2}},

		/* Stack */
		{0x48, {Opname::pha, Addressing::none, 3}},
		{0x08, {Opname::php, Addressing::none, 3}},
		{0x68, {Opname::pla, Addressing::none, 4}},
		{0x28, {Opname::plp, Addressing::none, 4}},

		/* Load/Store */
		// lda
		{0xa9, {Opname::lda, Addressing::immediate, 2}},
		{0xad, {Opname::lda, Addressing::absolute, 4}},
		{0xbd, {Opname::lda, Addressing::x_indexed_absolute, 4}},
		{0xb9, {Opname::lda, Addressing::y_indexed_absolute, 4}},
		{0xa5, {Opname::lda, Addressing::zero_page, 3}},
		{0xb5, {Opname::lda, Addressing::x_indexed_zero_page, 4}},
		{0xa1, {Opname::lda, Addressing::x_indexed_zero_page_indirect, 6}},
		{0xb1, {Opname::lda, Addressing::zero_page_indirect_y_indexed, 5}},
		// ldx
		{0xa2, {Opname::ldx, Addressing::immediate, 2}},
		{0xae, {Opname::ldx, Addressing::absolute, 4}},
		{0xbe, {Opname::ldx, Addressing::y_indexed_absolute, 4}},
		{0xa6, {Opname::ldx, Addressing::zero_page, 3}},
		{0xb6, {Opname::ldx, Addressing::y_indexed_zero_page, 4}},
		// ldy
		{0xa0, {Opname::ldy, Addressing::immediate, 2}},
		{0xac, {Opname::ldy, Addressing::absolute, 4}},
		{0xbc, {Opname::ldy, Addressing::x_indexed_absolute, 4}},
		{0xa4, {Opname::ldy, Addressing::zero_page, 3}},
		{0xb4, {Opname::ldy, Addressing::x_indexed_zero_page, 4}},
		// sta
		{0x8d, {Opname::sta, Addressing::absolute, 4}},
		{0x9d, {Opname::sta, Addressing::x_indexed_absolute, 5}},
		{0x99, {Opname::sta, Addressing::y_indexed_absolute, 5}},
		{0x85, {Opname::sta, Addressing::zero_page, 3}},
		{0x95, {Opname::sta, Addressing::x_indexed_zero_page, 4}},
		{0x81, {Opname::sta, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x91, {Opname::sta, Addressing::zero_page_indirect_y_indexed, 6}},
		// stx
		{0x8e, {Opname::stx, Addressing::absolute, 4}},
		{0x86, {Opname::stx, Addressing::zero_page, 3}},
		{0x96, {Opname::stx, Addressing::y_indexed_zero_page, 4}},
		// sty
		{0x8c, {Opname::sty, Addressing::absolute, 4}},
		{0x84, {Opname::sty, Addressing::zero_page, 3}},
		{0x94, {Opname::sty, Addressing::x_indexed_zero_page, 4}},

		/* Shift */
		// asl
		{0x0a, {Opname::asl, Addressing::none, 2}},
		{0x0e, {Opname::asl, Addressing::absolute, 6}},
		{0x1e, {Opname::asl, Addressing::x_indexed_absolute, 7}},
		{0x06, {Opname::asl, Addressing::zero_page, 5}},
		{0x16, {Opname::asl, Addressing::x_indexed_zero_page, 6}},
		// lsr
		{0x4a, {Opname::lsr, Addressing::none, 2}},
		{0x4e, {Opname::lsr, Addressing::absolute, 6}},
		{0x5e, {Opname::lsr, Addressing::x_indexed_absolute, 7}},
		{0x46, {Opname::lsr, Addressing::zero_page, 5}},
		{0x56, {Opname::lsr, Addressing::x_indexed_zero_page, 6}},
		// rol
		{0x2a, {Opname::rol, Addressing::none, 2}},
		{0x2e, {Opname::rol, Addressing::absolute, 6}},
		{0x3e, {Opname::rol, Addressing::x_indexed_absolute, 7}},
		{0x26, {Opname::rol, Addressing::zero_page, 5}},
		{0x36, {Opname::rol, Addressing::x_indexed_zero_page, 6}},
		// ror
		{0x6a, {Opname::ror, Addressing::none, 2}},
		{0x6e, {Opname::ror, Addressing::absolute, 6}},
		{0x7e, {Opname::ror, Addressing::x_indexed_absolute, 7}},
		{0x66, {Opname::ror, Addressing::zero_page, 5}},
		{0x76, {Opname::ror, Addressing::x_indexed_zero_page, 6}},

		/* Logic */
		// and
		{0x29, {Opname::andd, Addressing::immediate, 2}},
		{0x2d, {Opname::andd, Addressing::absolute, 4}},
		{0x3d, {Opname::andd, Addressing::x_indexed_absolute, 4}},
		{0x39, {Opname::andd, Addressing::y_indexed_absolute, 4}},
		{0x25, {Opname::andd, Addressing::zero_page, 3}},
		{0x35, {Opname::andd, Addressing::x_indexed_zero_page, 4}},
		{0x21, {Opname::andd, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x31, {Opname::andd, Addressing::zero_page_indirect_y_indexed, 5}},
		// bit
		{0x2c, {Opname::bit, Addressing::absolute, 4}},
		{0x24, {Opname::bit, Addressing::zero_page, 3}},
		// eor
		{0x49, {Opname::eor, Addressing::immediate, 2}},
		{0x4d, {Opname::eor, Addressing::absolute, 4}},
		{0x5d, {Opname::eor, Addressing::x_indexed_absolute, 4}},
		{0x59, {Opname::eor, Addressing::y_indexed_absolute, 4}},
		{0x45, {Opname::eor, Addressing::zero_page, 3}},
		{0x55, {Opname::eor, Addressing::x_indexed_zero_page, 4}},
		{0x41, {Opname::eor, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x51, {Opname::eor, Addressing::zero_page_indirect_y_indexed, 5}},
		// ora
		{0x09, {Opname::ora, Addressing::immediate, 2}},
		{0x0d, {Opname::ora, Addressing::absolute, 4}},
		{0x1d, {Opname::ora, Addressing::x_indexed_absolute, 4}},
		{0x19, {Opname::ora, Addressing::y_indexed_absolute, 4}},
		{0x05, {Opname::ora, Addressing::zero_page, 3}},
		{0x15, {Opname::ora, Addressing::x_indexed_zero_page, 4}},
		{0x01, {Opname::ora, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x11, {Opname::ora, Addressing::zero_page_indirect_y_indexed, 5}},

		/* Arithmetic */
		// adc
		{0x69, {Opname::adc, Addressing::immediate, 2}},
		{0x6d, {Opname::adc, Addressing::absolute, 4}},
		{0x7d, {Opname::adc, Addressing::x_indexed_absolute, 4}},
		{0x79, {Opname::adc, Addressing::y_indexed_absolute, 4}},
		{0x65, {Opname::adc, Addressing::zero_page, 3}},
		{0x75, {Opname::adc, Addressing::x_indexed_zero_page, 4}},
		{0x61, {Opname::adc, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x71, {Opname::adc, Addressing::zero_page_indirect_y_indexed, 5}},
		// sbc
		{0xe9, {Opname::sbc, Addressing::immediate, 2}},
		{0xed, {Opname::sbc, Addressing::absolute, 4}},
		{0xfd, {Opname::sbc, Addressing::x_indexed_absolute, 4}},
		{0xf9, {Opname::sbc, Addressing::y_indexed_absolute, 4}},
		{0xe5, {Opname::sbc, Addressing::zero_page, 3}},
		{0xf5, {Opname::sbc, Addressing::x_indexed_zero_page, 4}},
		{0xe1, {Opname::sbc, Addressing::x_indexed_zero_page_indirect, 6}},
		{0xf1, {Opname::sbc, Addressing::zero_page_indirect_y_indexed, 5}},
		// cmp
		{0xc9, {Opname::cmp, Addressing::immediate, 2}},
		{0xcd, {Opname::cmp, Addressing::absolute, 4}},
		{0xdd, {Opname::cmp, Addressing::x_indexed_absolute, 4}},
		{0xd9, {Opname::cmp, Addressing::y_indexed_absolute, 4}},
		{0xc5, {Opname::cmp, Addressing::zero_page, 3}},
		{0xd5, {Opname::cmp, Addressing::x_indexed_zero_page, 4}},
		{0xc1, {Opname::cmp, Addressing::x_indexed_zero_page_indirect, 6}},
		{0xd1, {Opname::cmp, Addressing::zero_page_indirect_y_indexed, 5}},
		// cpx
		{0xe0, {Opname::cpx, Addressing::immediate, 2}},
		{0xec, {Opname::cpx, Addressing::absolute, 4}},
		{0xe4, {Opname::cpx, Addressing::zero_page, 3}},
		// cpy
		{0xc0, {Opname::cpy, Addressing::immediate, 2}},
		{0xcc, {Opname::cpy, Addressing::absolute, 4}},
		{0xc4, {Opname::cpy, Addressing::zero_page, 3}},

		/* Control Flow */
		// brk
		{0x00, {Opname::brk, Addressing::none, 7}},
		// jmp
		{0x4c, {Opname::jmp, Addressing::absolute, 3}},
		{0x6c, {Opname::jmp, Addressing::absolute_indirect, 5}},
		// jsr
		{0x20, {Opname::jsr, Addressing::absolute, 6}},
		// rti
		{0x40, {Opname::rti, Addressing::none, 6}},
		// rts
		{0x60, {Opname::rts, Addressing::none, 6}},
	};

private:
	// Registers
	uint8_t A, X, Y, S;
	union {
		uint8_t P;
		struct __attribute__ ((packed)) {
			bool CF : 1;
			bool ZF : 1;
			bool IDF : 1;
			bool DF : 1;
			bool BF : 1;
			bool _P_UNUSED : 1;
			bool VF : 1;
			bool NF : 1;
		};
	};
	uint16_t PC;

	// Storage
	std::array<uint8_t, 0x1'00'00> RAM, ROM;

private:
	std::string out_buffer;

	Configuration conf {"src/ps11/6502.ini"};

private:
	void load(const std::array<uint8_t, 16>& dump)
	{
		begin();
		// ROM = dump;
	}

	bool load(std::string_view filename)
	{
		begin();

		std::ifstream file(filename.data(), std::ios::in | std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		file.seekg(0, std::ios::end);
		const size_t filesize = file.tellg();
		file.seekg(0);
		file.read(reinterpret_cast<char*>(ROM.data()), std::min(ROM.size(), filesize));

		return true;
	}

private:
	void begin()
	{
		A = X = Y = P = 0;
		S = 0xff;
		PC = 0;
		std::fill(RAM.begin(), RAM.end(), 0);
		std::fill(ROM.begin(), ROM.end(), 0);
	}

	uint16_t fetch_operand(Addressing addr, bool& page_cross)
	{
		uint16_t operand = 0;
		page_cross = false;

		auto _fetch_word = [&]() -> uint16_t {
			uint16_t value = fetch();
			value |= uint16_t(fetch()) << 8;
			return value;
		};

		uint16_t temp[2];
		switch (addr)
		{
			using enum Addressing;

		case none:
			break;

		case immediate:
			operand = fetch();
			break;

		case absolute:
			operand = _fetch_word();
			break;

		case x_indexed_absolute:
			operand = _fetch_word();
			temp[0] = operand & 0xff'00;
			operand += X;
			temp[1] = operand & 0xff'00;
			page_cross = temp[0] != temp[1];
			break;

		case y_indexed_absolute:
			operand = _fetch_word();
			temp[0] = operand & 0xff'00;
			operand += Y;
			temp[1] = operand & 0xff'00;
			page_cross = temp[0] != temp[1];
			break;

		case absolute_indirect:
			operand = _fetch_word();
			operand = RAM[operand] | uint16_t(RAM[operand+1]);
			break;

		case zero_page:
			operand = fetch();
			break;

		case x_indexed_zero_page:
			operand = fetch();
			operand += X;
			operand &= 0x00'ff;
			break;

		case y_indexed_zero_page:
			operand = fetch();
			operand += Y;
			operand &= 0x00'ff;
			break;

		case x_indexed_zero_page_indirect:
			operand = fetch();
			operand += X;
			operand &= 0x00'ff;
			operand = RAM[operand] | uint16_t(RAM[operand+1]);
			break;

		case zero_page_indirect_y_indexed:
			operand = fetch();
			operand = RAM[operand] | uint16_t(RAM[operand+1]);
			temp[0] = operand & 0xff'00;
			operand += Y;
			temp[1] = operand & 0xff'00;
			page_cross = temp[0] != temp[1];
			break;

		case relative:
			// NOTE: Probably not properly implemented
			operand = int8_t(fetch());
			{
				temp[0] = PC + operand;
				page_cross = (PC & 0xff'00) != (temp[0] & 0xff'00);
			}
			break;
		}

		return operand;
	}

	uint8_t decode_execute(uint8_t opcode)
	{
		auto search = opcode_table.find(opcode);
		iassert(search != opcode_table.end(), "Unimplemented instruction {:#x} at PC {:#x}", opcode, PC-1);
		const auto [opname, addr, ideal_cycles] = search->second;

		bool page_cross;
		const uint16_t operand = fetch_operand(addr, page_cross);

		uint8_t cycles = ideal_cycles;

		auto _branch_considered = [&](uint16_t operand) {
			PC += operand;
			cycles++;
		};

		enum _Flags : unsigned {
			_F_N = 1, _F_Z = 2
		};
		auto _update_flags = [&](unsigned flags, uint8_t value) {
			if (flags & _F_N)
				NF = value & (1 << 7);
			if (flags & _F_Z)
				ZF = value == 0;
		};

		auto _push = [&](uint8_t value) {
			// iassert(S != 0, "Stacking overflow at PC {:#x}", PC);
			RAM[0x0100 + S] = value;
			S--;
		};
		auto _pop = [&]() -> uint8_t {
			S++;
			return RAM[0x0100 + S];
		};

		uint8_t temp;
		switch (opname)
		{
			using enum Opname;

		/* NOP */
		case nop: break;

		/* Flags */
		case clc: CF = false; break;
		case cld: DF = false; break;
		case cli: IDF = false; break;
		case clv: VF = false; break;
		case sec: CF = true; break;
		case sed: DF = true; iassert(false, "Decimal mode unimplemented"); break;
		case sei: IDF = true; break;

		/* Control Flow: Branch */
		case bcc: if (!CF) _branch_considered(operand); break;
		case bne: if (!ZF) _branch_considered(operand); break;
		case bpl: if (!NF) _branch_considered(operand); break;
		case bvc: if (!VF) _branch_considered(operand); break;
		case bcs: if (CF) _branch_considered(operand); break;
		case beq: if (ZF) _branch_considered(operand); break;
		case bmi: if (NF) _branch_considered(operand); break;
		case bvs: if (VF) _branch_considered(operand); break;

		/* Arithmetic: Inc/Dec */
		case dec: RAM[operand]--; _update_flags(_F_N | _F_Z, RAM[operand]); break;
		case dex: X--; _update_flags(_F_N | _F_Z, X); break;
		case dey: Y--; _update_flags(_F_N | _F_Z, Y); break;
		case inc: RAM[operand]++; _update_flags(_F_N | _F_Z, RAM[operand]); break;
		case inx: X++; _update_flags(_F_N | _F_Z, X); break;
		case iny: Y++; _update_flags(_F_N | _F_Z, Y); break;

		/* Transfer */
		case tax: X = A; _update_flags(_F_N | _F_Z, X); break;
		case tay: Y = A; _update_flags(_F_N | _F_Z, Y); break;
		case tsx: X = S; _update_flags(_F_N | _F_Z, X); break;
		case txa: A = X; _update_flags(_F_N | _F_Z, A); break;
		case txs: S = X; break;
		case tya: A = Y; _update_flags(_F_N | _F_Z, A); break;

		/* Stack */
		case pha: _push(A); break;
		case php: {
			auto prev_BF = BF;
			BF = true;
			_push(P);
			BF = prev_BF;
		} break;
		case pla: A = _pop(); _update_flags(_F_N | _F_Z, A); break;
		case plp: P = _pop(); break;

		/* Load/Store */
		case lda:
			A = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, A);
			break;
		case ldx:
			X = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, X);
			break;
		case ldy:
			Y = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, Y);
			break;
		case sta: RAM[operand] = A; break;
		case stx: RAM[operand] = X; break;
		case sty: RAM[operand] = Y; break;

		/* Shift */
		case asl:
		case rol: {
			uint8_t *poperand = addr == Addressing::none ? &A : &RAM[operand];
			CF = *poperand & (1 << 7);
			*poperand <<= 1;
			if (opname == rol) *poperand |= uint8_t(CF);
			_update_flags(_F_N | _F_Z, *poperand);
		} break;
		case lsr:
		case ror: {
			uint8_t *poperand = addr == Addressing::none ? &A : &RAM[operand];
			CF = *poperand & 1;
			*poperand >>= 1;
			if (opname == ror) *poperand |= uint8_t(CF) << 7;
			_update_flags(_F_N | _F_Z, *poperand);
		} break;

		/* Logic */
		case andd: temp = addr == Addressing::immediate ? operand : RAM[operand]; A &= temp; _update_flags(_F_N | _F_Z, temp); break;
		case bit: {
			uint8_t test = A & RAM[operand];
			VF = RAM[operand] & (1 << 6);
			_update_flags(_F_N, RAM[operand]);
			_update_flags(_F_Z, test);
	  	} break;
		case eor: temp = addr == Addressing::immediate ? operand : RAM[operand]; A ^= temp; _update_flags(_F_N | _F_Z, temp); break;
		case ora: temp = addr == Addressing::immediate ? operand : RAM[operand]; A |= temp; _update_flags(_F_N | _F_Z, temp); break;

		/* Arithmetic */
		case adc: {
			int16_t final_operand = addr == Addressing::immediate ? int8_t(operand) : int8_t(RAM[operand]);
			uint16_t sum = uint16_t(int16_t(A)) + uint16_t(final_operand) + uint16_t(CF);

			CF = sum > 0xff;
			VF = (sum & 0x80) != (A & 0x80);
			A = sum;
			NF = A & (1 << 7);
			ZF = A == 0;
		} break;
		case sbc: {
			int16_t final_operand = addr == Addressing::immediate ? int8_t(operand) : int8_t(RAM[operand]);
			uint16_t diff = uint16_t(int16_t(A)) - uint16_t(final_operand) - uint16_t(CF); // FIXME: Is this OK?
			int16_t sdiff = diff;

			VF = sdiff > 127 | sdiff < -127;
			A = diff;
			CF = !(A & (1 << 7));
			NF = A & (1 << 7);
			ZF = A == 0;
		} break; 
		case cmp: {
			uint8_t final_operand = addr == Addressing::immediate ? operand : RAM[operand];
			uint8_t test = A - final_operand;

			// NOTE: Could be optimized
			CF = A >= final_operand;
			NF = test & (1 << 7);
			ZF = test == 0;
		} break;
		case cpx: {
			uint8_t final_operand = addr == Addressing::immediate ? operand : RAM[operand];
			uint8_t test = X - final_operand;

			// NOTE: Could be optimized
			CF = X >= final_operand;
			NF = test & (1 << 7);
			ZF = test == 0;
		} break;
		case cpy: {
			uint8_t final_operand = addr == Addressing::immediate ? operand : RAM[operand];
			uint8_t test = Y - final_operand;

			// NOTE: Could be optimized
			CF = Y >= final_operand;
			NF = test & (1 << 7);
			ZF = test == 0;
		} break;

		/* Control Flow */
		case brk: {
			fetch(); // brk is a 2-byte opcode

			_push(PC >> 8);
			_push(PC & 0xff);

			auto prev_BF = BF;
			BF = true;
			_push(P);
			BF = prev_BF;

			IDF = true;

			PC = RAM[0xfffe] | RAM[0xffff] << 8;
		} break;
		case jmp: PC = operand; break;
		case jsr: {
			_push(PC >> 8);
			_push(PC & 0xff);
			PC = operand;
		} break;
		case rti: {
			P = _pop(); BF = false;
			PC = _pop() | _pop() << 8;
		} break;
		case rts: PC = _pop() | _pop() << 8; break;
		}

		cycles += uint8_t(page_cross);

		static bool suppress_instruction;
		static bool is_si_pulled = false;
		if (!is_si_pulled) {
			suppress_instruction = conf.get<long>("suppress instruction");
			is_si_pulled = true;
		}

		if (!suppress_instruction) {
			const char *bold = "\033[1;32m", *reset = "\033[0m";
			out_buffer += std::format("\n{}-> {} 0x{:x},{}{}\n", bold, opname_str[static_cast<int>(opname)], operand, operand, reset);
		}

		return cycles;
	}

	uint8_t fetch()
	{
		return ROM[PC++];
	}

	uint8_t step()
	{
		uint8_t opcode = fetch();
		uint8_t cycles = decode_execute(opcode);

		return cycles;
	}

public:
	PS11(int argc, char** argv)
	{
		iassert(argc > 1, "No ROM provided");
		iassert(load(argv[1]), "Couldn't load {}", argv[1]);
	}

	void print_info()
	{
		const char *bold = "\033[1;31m", *reset = "\033[0m";

		const int _CF = CF, _ZF = ZF, _IDF = IDF, _DF = DF, _BF = BF, _VF = VF, _NF = NF;
		out_buffer += '\n';
		out_buffer += std::format("{}A:{} 0x{:X},{},{} {}X:{} 0x{:X},{},{} {}Y:{} 0x{:X},{},{}\n",
			bold, reset, A, A, (int8_t)A,
			bold, reset, X, X, (int8_t)X,
			bold, reset, Y, Y, (int8_t)Y);
		out_buffer += std::format("{}S:{} 0x{:0>2X} {}PC:{} 0x{:0>4X}\n", bold, reset, S, bold, reset, PC);
		out_buffer += std::format("{}CF:{} {} {}ZF:{} {} {}IDF:{} {} {}DF:{} {} {}BF:{} {} {}VF:{} {} {}NF:{} {}\n",
			bold, reset, _CF, bold, reset, _ZF, bold, reset, _IDF,
			bold, reset, _DF, bold, reset, _BF, bold, reset, _VF, bold, reset, _NF);
	};

	void init() {}
	void run()
	{
		[[maybe_unused]] auto _load_new = [&]() {
			std::string filename;
			std::print(stderr, "Dump to load? ");

			if (!std::getline(std::cin, filename) or filename.size() < 0) return;
			if (!load(filename))
				std::println(stderr, "Couldn't load {}", filename);

			std::cin.clear();
		};

		[[maybe_unused]] auto _input = [&](char& c) -> bool {
			do {
				std::cin.get(c);
			} while (c == ' ' or c == '\t');

			if (std::cin.fail())
				return false;

			std::cin.clear();
			return true;
		};

		const float clock_rate = conf.get<long>("clock rate");
		std::chrono::nanoseconds ns_per_tick (uint64_t(1e9 / clock_rate));

		const bool suppress_info = conf.get<long>("suppress info");

		bool quit = false;
		while (!quit)
		{
			auto then = std::chrono::high_resolution_clock::now();

			uint8_t cycles = 0;
			{
				cycles = step();
				if (!suppress_info)
					print_info();
			}

			std::print("{}", out_buffer);
			out_buffer.clear();
			std::fflush(stdout);

			auto now = std::chrono::high_resolution_clock::now();

			auto actual_duration = now - then;
			auto ideal_duration = cycles * ns_per_tick;
			// std::println("{:.3f}%", 100.0 * actual_duration.count() / double(ideal_duration.count()));
			std::this_thread::sleep_for(ideal_duration - actual_duration);
		}
	}
};
