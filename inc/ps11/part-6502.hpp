#pragma once

#include "pch.hpp"
#include "utility.hpp"

class PS11
{
private:
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
		{0xce, {Opname::dec, Addressing::absolute, 6}},
		{0xde, {Opname::dec, Addressing::x_indexed_absolute, 7}},
		{0xc6, {Opname::dec, Addressing::zero_page, 5}},
		{0xd6, {Opname::dec, Addressing::x_indexed_zero_page, 6}},
		{0xca, {Opname::dex, Addressing::none, 2}},
		{0x88, {Opname::dey, Addressing::none, 2}},
		{0xee, {Opname::inc, Addressing::absolute, 6}},
		{0xfe, {Opname::inc, Addressing::x_indexed_absolute, 7}},
		{0xe6, {Opname::inc, Addressing::zero_page, 5}},
		{0xf6, {Opname::inc, Addressing::x_indexed_zero_page, 6}},
		{0xe8, {Opname::inx, Addressing::none, 2}},
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
		{0xa9, {Opname::lda, Addressing::immediate, 2}},
		{0xad, {Opname::lda, Addressing::absolute, 4}},
		{0xbd, {Opname::lda, Addressing::x_indexed_absolute, 4}},
		{0xb9, {Opname::lda, Addressing::y_indexed_absolute, 4}},
		{0xa5, {Opname::lda, Addressing::zero_page, 3}},
		{0xb5, {Opname::lda, Addressing::x_indexed_zero_page, 4}},
		{0xa1, {Opname::lda, Addressing::x_indexed_zero_page_indirect, 6}},
		{0xb1, {Opname::lda, Addressing::zero_page_indirect_y_indexed, 5}},
		{0xa2, {Opname::ldx, Addressing::immediate, 2}},
		{0xae, {Opname::ldx, Addressing::absolute, 4}},
		{0xbe, {Opname::ldx, Addressing::y_indexed_absolute, 4}},
		{0xa6, {Opname::ldx, Addressing::zero_page, 3}},
		{0xb6, {Opname::ldx, Addressing::y_indexed_zero_page, 4}},
		{0xa0, {Opname::ldy, Addressing::immediate, 2}},
		{0xac, {Opname::ldy, Addressing::absolute, 4}},
		{0xbc, {Opname::ldy, Addressing::x_indexed_absolute, 4}},
		{0xa4, {Opname::ldy, Addressing::zero_page, 3}},
		{0xb4, {Opname::ldy, Addressing::x_indexed_zero_page, 4}},
		{0x8d, {Opname::sta, Addressing::absolute, 4}},
		{0x9d, {Opname::sta, Addressing::x_indexed_absolute, 5}},
		{0x99, {Opname::sta, Addressing::y_indexed_absolute, 5}},
		{0x85, {Opname::sta, Addressing::zero_page, 3}},
		{0x95, {Opname::sta, Addressing::x_indexed_zero_page, 4}},
		{0x81, {Opname::sta, Addressing::x_indexed_zero_page_indirect, 6}},
		{0x91, {Opname::sta, Addressing::zero_page_indirect_y_indexed, 6}},
		{0x8e, {Opname::stx, Addressing::absolute, 4}},
		{0x86, {Opname::stx, Addressing::zero_page, 3}},
		{0x96, {Opname::stx, Addressing::y_indexed_zero_page, 4}},
		{0x8c, {Opname::sty, Addressing::absolute, 4}},
		{0x84, {Opname::sty, Addressing::zero_page, 3}},
		{0x94, {Opname::sty, Addressing::x_indexed_zero_page, 4}},

		/* Shift */
		{0x0a, {Opname::asl, Addressing::none, 2}},
		{0x0e, {Opname::asl, Addressing::absolute, 6}},
		{0x1e, {Opname::asl, Addressing::x_indexed_absolute, 7}},
		{0x06, {Opname::asl, Addressing::zero_page, 5}},
		{0x16, {Opname::asl, Addressing::x_indexed_zero_page, 6}},
		{0x4a, {Opname::lsr, Addressing::none, 2}},
		{0x4e, {Opname::lsr, Addressing::absolute, 6}},
		{0x5e, {Opname::lsr, Addressing::x_indexed_absolute, 7}},
		{0x46, {Opname::lsr, Addressing::zero_page, 5}},
		{0x56, {Opname::lsr, Addressing::x_indexed_zero_page, 6}},
		{0x2a, {Opname::rol, Addressing::none, 2}},
		{0x2e, {Opname::rol, Addressing::absolute, 6}},
		{0x3e, {Opname::rol, Addressing::x_indexed_absolute, 7}},
		{0x26, {Opname::rol, Addressing::zero_page, 5}},
		{0x36, {Opname::rol, Addressing::x_indexed_zero_page, 6}},
		{0x6a, {Opname::ror, Addressing::none, 2}},
		{0x6e, {Opname::ror, Addressing::absolute, 6}},
		{0x7e, {Opname::ror, Addressing::x_indexed_absolute, 7}},
		{0x66, {Opname::ror, Addressing::zero_page, 5}},
		{0x76, {Opname::ror, Addressing::x_indexed_zero_page, 6}},
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
			bool EF : 1;
			bool VF : 1;
			bool NF : 1;
		};
	};
	uint16_t PC;

	// Storage
	std::array<uint8_t, 0xff'ff> RAM, ROM;

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
			operand = int16_t(fetch());
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
			cycles += 1 + uint8_t(page_cross);
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

		uint8_t *ptemp;
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
		case sed: DF = true; break;
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
		case php: _push(P); break;
		case pla: A = _pop(); _update_flags(_F_N | _F_Z, A); break;
		case plp: P = _pop(); break;

		/* Load/Store */
		case lda:
			A = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, A);
			cycles += uint8_t(page_cross);
			break;
		case ldx:
			X = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, X);
			cycles += uint8_t(page_cross);
			break;
		case ldy:
			Y = addr == Addressing::immediate ? operand : RAM[operand];
			_update_flags(_F_N | _F_Z, Y);
			cycles += uint8_t(page_cross);
			break;
		case sta: RAM[operand] = A; break;
		case stx: RAM[operand] = X; break;
		case sty: RAM[operand] = Y; break;

		case asl:
		case rol:
			ptemp = addr == Addressing::none ? &A : &RAM[operand];
			CF = *ptemp & (1 << 7);
			*ptemp <<= 1;
			if (opname == rol) *ptemp |= uint8_t(CF);
			_update_flags(_F_N | _F_Z, *ptemp);
			break;
		case lsr:
		case ror:
			ptemp = addr == Addressing::none ? &A : &RAM[operand];
			CF = *ptemp & 1;
			*ptemp >>= 1;
			if (opname == ror) *ptemp |= uint8_t(CF) << 7;
			_update_flags(_F_N | _F_Z, *ptemp);
			break;
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
		if (argc > 1) {
			if (!load(argv[1]))
				std::println(stderr, "Couldn't load {}", argv[1]);
		}
	}

	void init()
	{
	}

	void run()
	{
		auto _print_regs_flags = [&]() {
		};

		auto _load_new = [&]() {
			std::string filename;
			std::print(stderr, "Dump to load? ");

			if (!std::getline(std::cin, filename) or filename.size() < 0) return;
			if (!load(filename))
				std::println(stderr, "Couldn't load {}", filename);

			std::cin.clear();
		};

		auto _input = [&](char& c) -> bool {
			do {
				std::cin.get(c);
			} while (c == ' ' or c == '\t');

			if (std::cin.fail())
				return false;

			std::cin.clear();
			return true;
		};

		unsigned speed = 5;  // Hertz
		std::chrono::nanoseconds sleep_for (uint64_t(1e9 / double(speed)));

		bool quit = false;
		while (!quit)
		{
			if (false)
			{
			}
			else if (false)
			{
			}
			else
			{
				auto then = std::chrono::high_resolution_clock::now();
				std::println();
				// advance();
				_print_regs_flags();
				auto now = std::chrono::high_resolution_clock::now();

				std::this_thread::sleep_for(sleep_for - (now - then));
			}
		}
	}
};
