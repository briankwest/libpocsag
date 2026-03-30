#include "pocsag_internal.h"

uint32_t pocsag_cw_address(uint32_t address, pocsag_func_t func)
{
	uint32_t addr_upper = (address >> 3) & 0x3FFFFu;
	uint32_t data21 = (addr_upper << 2) | (func & 3u);
	return pocsag_codeword_build(data21);
}

uint32_t pocsag_cw_message(uint32_t data20)
{
	uint32_t data21 = (1u << 20) | (data20 & 0xFFFFFu);
	return pocsag_codeword_build(data21);
}
