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
		// file.read(reinterpret_cast<char*>(ROM.data()), std::min(size_t(16), filesize));

		return true;
	}

private:
	void begin()
	{
		A = X = Y = S = P = 0;
		PC = 0;
		std::fill(RAM.begin(), RAM.end(), 0);
		std::fill(ROM.begin(), ROM.end(), 0);
	}

	uint16_t fetch_operand(Addressing addr)
	{
		uint16_t operand = 0;

		auto _fetch_word = [&]() -> uint16_t {
			uint16_t value = fetch();
			value |= uint16_t(fetch()) << 8;
			return value;
		};

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
			operand += X;
			break;

		case y_indexed_absolute:
			operand = _fetch_word();
			operand += Y;
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
			operand += Y;
			break;

		case relative:
			operand = fetch();
			break;
		}

		return operand;
	}

	uint8_t decode_execute(uint8_t opcode)
	{
		auto search = opcode_table.find(opcode);
		iassert(search != opcode_table.end(), "Unimplemented instruction {:#x} at PC {:#x}", opcode, PC-1);
		const auto [opname, addr, ideal_cycles] = search->second;
		const uint16_t operand = fetch_operand(addr);

		uint8_t cycles = ideal_cycles;

		auto _branch_considered = [&](uint16_t operand) {
			cycles += 1 + ((PC & 0xff00) == (operand & 0xff00) ? 0 : 1);
			PC += int16_t(operand);
		};

		enum _Flags : unsigned {
			_F_N = 1, _F_Z = 2
		};
		auto _update_flags = [&](unsigned flags, auto value) {
			if (flags & _F_N)
				NF = value < 0;
			if (flags & _F_Z)
				ZF = value == 0;
		};

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
		}

		return cycles;
	}

	uint8_t fetch()
	{
		return ROM[PC++];
	}

	// returns: extra cycles consumed from the 2 base cycles
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
