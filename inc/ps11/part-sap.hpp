#pragma once

#include "pch.hpp"

class PS11
{
private:
	// Registers
	uint8_t PC, A, OUT;

	// RAM
	std::array<uint8_t, 16> RAM, ROM;

	// Flags
	bool ZF, CF;

	// Internal state
	bool is_halted = true;

private:
	void load(const std::array<uint8_t, 16>& dump)
	{
		begin();
		ROM = dump;
		is_halted = false;
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
		file.read(reinterpret_cast<char*>(ROM.data()), std::min(size_t(16), filesize));

		is_halted = false;
		return true;
	}

private:
	void begin()
	{
		is_halted = true;
		PC = A = OUT = 0;
		ZF = CF = false;
		std::fill(RAM.begin(), RAM.end(), 0);
		std::fill(ROM.begin(), ROM.end(), 0);
	}

	void advance()
	{
		const uint8_t instruction = ROM[PC];
		const uint8_t opcode = instruction >> 4;
		const uint8_t id = instruction & 0xf;

		PC++;
		PC %= 16;

		auto _log = [&](std::string_view name) {
			std::println("{} {}", name, id);
		};

		switch (opcode)
		{
		case 0x0: // nop
			_log("NOP");
			break;

		case 0x1: // lda
			_log("LDA");
			A = RAM[id];
			break;

		case 0x2: // add
			_log("ADD");
			CF = A > 0xff - RAM[id]; // A + B > 0xff ? aka overflow
			A += RAM[id];
			ZF = A == 0;
			break;

		case 0x3: // sub
			_log("SUB");
			CF = A >= RAM[id]; // A >= B ? aka no underflow
			A -= RAM[id];
			ZF = A == 0;
			break;

		case 0x4: // sta
			_log("STA");
			RAM[id] = A; break;

		case 0x5: // ldi
			_log("LDI");
			A = id;
			break;

		case 0x6: // jmp
			_log("JMP");
			PC = id;
			break;

		case 0x7: // jc
			_log("JC");
			if (CF) PC = id;
			break;

		case 0x8: // jz
			_log("JZ");
			if (ZF) PC = id;
			break;

		case 0xe: // out
			_log("OUT");
			OUT = A;
			break;

		case 0xf: // hlt
			_log("HLT");
			is_halted = true;
			break;
		}
	}

public:
	PS11(int argc, char** argv)
	{
		if (argc > 1) {
			if (!load(argv[1]))
				std::println("Couldn't load {}", argv[1]);
		}
	}

	void init()
	{
	}

	void run()
	{
		auto _print_regs_flags = [&]() {
			std::println("Registers: PC = {}, A = {:#x},{},{}, OUT = {:#x},{},{}", PC, A, A, (int8_t)A, OUT, OUT, (int8_t)OUT);
			std::println("Flags: ZF = {}, CF = {}", ZF, CF);
			std::print("RAM:");
			for (unsigned i=0; i < 4; i++)
			{
				std::print("\n{:0>4x}: ", i*4);
				for (unsigned j=0; j < 4; j++)
					std::print("0x{:0>4x} ", RAM[i*4 + j]);
			}
			std::println();
		};

		auto _load_new = [&]() {
			std::string filename;
			std::print("Dump to load? ");

			if (!std::getline(std::cin, filename) or filename.size() < 0) return;
			if (!load(filename))
				std::println("Couldn't load {}", filename);

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

		bool quit = false;
		while (!quit)
		{
			if (is_halted)
			{
				_print_regs_flags();

				char c;
				if (!_input(c)) continue;

				switch (c)
				{
				case 'q':
					quit = true;
					break;
				default:
					_load_new();
					break;
				}
			}
			else
			{
				_print_regs_flags();

				char c;
				if (!_input(c)) continue;

				switch (c)
				{
				case '\n':
					advance();
					break;
				case 'l':
					_load_new();
					break;
				case 'h':
					is_halted = true;
					break;
				case 'q':
					quit = true;
					break;
				}
			}
		}
	}
};
