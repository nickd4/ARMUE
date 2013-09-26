#include "arm_v7m_ins_implement.h"
#include "error_code.h"
#include "memory_map.h"
#include "cpu.h"
#include <assert.h>

void LSL_C(uint32_t val, int shift, _O uint32_t* result, _O int *carry_out)
{
	*carry_out = 0;
	if(shift > 0){
		*carry_out = val >> (32-shift) & BIT_0;
	}

	*result = val << shift;
}

void LSR_C(uint32_t val, int shift, _O uint32_t* result, _O int *carry_out)
{
	*carry_out = 0;
	if(shift > 0){
		*carry_out = val >> (shift - 1) & BIT_0;
	}

	*result = val >> shift;
}

void ASR_C(uint32_t val, int shift, _O uint32_t* result, _O int *carry_out)
{
	*carry_out = 0;
	if(shift > 0){
		*carry_out = val >> (shift - 1) & BIT_0;
	}
// MS VC will ASR signed int
#ifdef _MSC_VER	
	*result = (int32_t)val >> shift;
#else
#error Unknow compiler
#endif
}

void ROR_C(uint32_t val, int shift, _O uint32_t* result, _O int *carry_out)
{
	assert(shift != 0);
	*carry_out = val >> (shift - 1) & BIT_0;
	uint32_t shift_out = val << (32-shift);
	*result = val >> shift | shift_out;
}

void Shift_C(uint32_t val, SRType type, int amount, int carry_in, _O uint32_t* result, _O int *carry_out)
{
	if(amount == 0){
		*result = val;
		*carry_out = carry_in;
		return;
	}

	switch(type){
	case SRType_LSL:
		LSL_C(val, amount, result, carry_out);
		break;
	case SRType_LSR:
		LSR_C(val, amount, result, carry_out);
		break;
	case SRType_ASR:
		ASR_C(val, amount, result, carry_out);
		break;
	case SRType_ROR:
		ROR_C(val, amount, result, carry_out);
		break;
	case SRType_RRX:
		break;
	default:
		break;
	}
}

inline void Shift(uint32_t val, SRType type, int amount, int carry_in, _O uint32_t* result)
{
	int carry_out;
	Shift_C(val, type, amount, carry_in, result, &carry_out);
}

inline uint8_t CurrentCond(armv7m_reg_t* regs)
{
	return (GET_ITSTATE(regs)>>4) & 0xF;
}

uint8_t ConditionPassed(uint8_t branch_cond, armv7m_reg_t* regs)
{
	if(!InITBlock(regs)){
		return 1;
	}
	uint8_t cond;
	if(branch_cond != 0){
		cond = branch_cond;
	}else{
		cond = CurrentCond(regs);
	}
	uint8_t result;
	switch(cond>>1){
	case 0:
		result = GET_APSR_Z(regs) == 1;	break;
	case 1:
		result = GET_APSR_C(regs) == 1; break;
	case 2:
		result = GET_APSR_N(regs) == 1; break;
	case 3:
		result = GET_APSR_V(regs) == 1; break;
	case 4:
		result = (GET_APSR_C(regs) == 1) && (GET_APSR_Z(regs) == 1); break;
	case 5:
		result = GET_APSR_N(regs) == GET_APSR_V(regs); break;
	case 6:
		result = (GET_APSR_N(regs) == GET_APSR_V(regs)) && (GET_APSR_Z(regs) == 0); break;
	case 7:
		result = 1; break;
	default:
		break;
	}

	if((cond & 0x1) && cond != 0xFF){
		result = !result;
	}

	return result;
}

/*<<ARMv7-M Architecture Reference Manual A2-43>>*/
inline void AddWithCarry(uint32_t op1, uint32_t op2, uint32_t carry_in, uint32_t* result, uint32_t* carry_out, uint32_t *overflow)
{
	/* uint64_t is 64bit */
	if(sizeof(uint64_t) == 8){
		uint64_t unsigned_sum = (uint64_t)op1 + (uint64_t)op2 + (uint64_t)carry_in;
		int64_t op1_64 = (int32_t)op1;
		int64_t op2_64 = (int32_t)op2;
		int64_t signed_sum = op1_64 + op2_64 + (int64_t)carry_in;
		uint32_t _result = unsigned_sum & 0xFFFFFFFFL;
		*overflow = (signed_sum == (int32_t)_result)? 0 : 1;
		*carry_out = (unsigned_sum == (uint64_t)_result) ? 0 : 1;
		*result = _result;

	/* general AddWithCarry if there isn't uint64_t */
	}else{
		// spilit 32 bit number to 2-16bit number
		uint16_t op1_low = op1 & 0xFFFF;
		uint16_t op1_high = (uint32_t)op1 >> 16;
		uint16_t op2_low = op2 & 0xFFFF;
		uint16_t op2_high = (op2) >> 16;
		
		// calculate low 16 bit carry
		int low_carry;
		uint32_t ulow_sum = (uint32_t)op1_low + (uint32_t)op2_low + (uint32_t)carry_in;
		uint32_t low_result = ulow_sum & 0xFFFF;
		low_carry = (ulow_sum == (uint32_t)low_result) ? 0 : 1;

		// calculate high 16 bit carry and overflow
		uint32_t uhigh_sum = (uint32_t)op1_high + (uint32_t)op2_high + (uint32_t)low_carry;
		int32_t op1_high_32 = (int16_t)op1_high;
		int32_t op2_high_32 = (int16_t)op2_high;
		int32_t shigh_sum = op1_high_32 + op2_high_32 + (int32_t)low_carry;
		uint16_t high_result = uhigh_sum & 0xFFFF;
		*overflow = (shigh_sum == (int16_t)high_result) ? 0 : 1;
		*carry_out = (uhigh_sum == (uint32_t)high_result) ? 0 : 1;
		*result = op1 + op2 + carry_in;
	}
}

/* <<ARMv7-M Architecture Reference Manual 630>> */
inline void BranchTo(uint32_t address, armv7m_reg_t* regs)
{
	SET_REG_VAL(regs, PC_INDEX, address);
}

/* <<ARMv7-M Architecture Reference Manual 47>> */
inline void BranchWritePC(uint32_t address, armv7m_reg_t* regs)
{
	BranchTo(DOWN_ALIGN(address, 1), regs);
}

/* <<ARMv7-M Architecture Reference Manual 48>> */
inline void ALUWritePC(uint32_t address, armv7m_reg_t* regs)
{
	BranchWritePC(address, regs);
}

/* <<ARMv7-M Architecture Reference Manual 47>> */
void BXWritePC(uint32_t address, armv7m_reg_t* regs, armv7m_state* state)
{
	uint32_t Tflag;
	int CurrentMode = state->mode;
	if(CurrentMode == MODE_HANDLER &&
		((address >> 28) & 0xFul) == 0xFul){
			//TODO: ExceptionReturn();
	}else{
		Tflag = address & 0x1ul;
		SET_EPSR_T(regs, Tflag);
		if(Tflag == 0){
			//TODO: UsageFault(Invalid_State);
		}

		BranchTo(DOWN_ALIGN(address, 1), regs);
	}
}

/* <<ARMv7-M Architecture Reference Manual 47>> */
inline void LoadWritePC(uint32_t address, armv7m_reg_t* regs, armv7m_state* state)
{
	BXWritePC(address, regs, state);
}

/* <<ARMv7-M Architecture Reference Manual B2-694>> */
bool_t FindPriv(armv7m_reg_t* regs, armv7m_state* state)
{
	bool_t ispriv;
	if(state->mode == MODE_HANDLER ||
		(state->mode == MODE_THREAD && GET_CONTROL_PRIV(regs) == 0)){
		ispriv = TRUE;
	}else{
		ispriv = FALSE;
	}
	return ispriv;
}

/* <<ARMv7-M Architecture Reference Manual B2-696>> */
int MemU_with_priv(uint32_t address, int size, _IO uint8_t* buffer, bool_t priv, int type, armv7m_reg_t* regs, memory_map_t* memory)
{
	int retval = 0;
	if(0){
		/* TODO: here is the condition of CCR.UNALIGN_TRP == 1*/
		// ExceptionTaken(UsageFault);
		return 0;
	}else{
		/* Since unaligned visit is not different from aligned one in emulation, we just do the samething. */
		if(type == MEM_READ){
			// TODO: Send an io request here and delay the real access to when the instruction finished.
			// send_io_request(address, size, buffer, io_info, type);
			retval = get_from_memory(address, size, buffer, memory);
			/* reverse big endian */
		}else if(type == MEM_WRITE){
			/* reverse big endian */
			retval = set_to_memory(address, size, buffer, memory);
		}
	}

	return retval;
}

/* TODO: Memory access is not complete for checking some flags like ALIGN and ENDIANs */
/* <<ARMv7-M Architecture Reference Manual B2-696>> */
int MemU(uint32_t address, int size, _IO uint8_t* buffer, int type, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory)
{
	return MemU_with_priv(address, size, buffer, FindPriv(regs, state), type, regs, memory);
}

int MemA_with_priv(uint32_t address, int size, _IO uint8_t* buffer, bool_t priv, int type, armv7m_reg_t* regs, memory_map_t* memory)
{
	int retval;
	if(address != Align(address, size)){
		//TODO: These registers are memory mapped. So they need to be treated like peripherals. 
		/* UFSR.UNALIGENED = '1'
		   ExceptionTaken(UsageFault)
		 */
		return 0;
	}
	// ValidateAddress() using MPU
	if(type == MEM_READ){
		retval = get_from_memory(address, size, buffer, memory);
		// if AIRCR.ENDIANNESS == 1 then
		//		value = BigEndianReverse(value, size);
	}else if(type == MEM_WRITE){
		// if AIRCR.ENDIANNESS == 1 then
		// reverse big endian
		retval = set_to_memory(address, size, buffer, memory);
	}
	return retval;
}

int MemA(uint32_t address, int size, _IO uint8_t* buffer, int type, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory)
{
	return MemA_with_priv(address, size, buffer, FindPriv(regs, state), type, regs, memory);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-334>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry) = Shift_C(R[m], SRType_LSL, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
*************************************/
void _lsl_imm(uint32_t imm, uint32_t Rm, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	int carry = 0;
	uint32_t result;
	
	if(!ConditionPassed(0, regs)){
		return;
	}

	Shift_C(GET_REG_VAL(regs, Rm), SRType_LSL, imm, GET_APSR_C(regs), &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-338>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry) = Shift_C(R[m], SRType_LSR, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
*************************************/
void _lsr_imm(uint32_t imm, uint32_t Rm, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	int carry = 0;
	uint32_t result;

	if(!ConditionPassed(0, regs)){
		return;
	}

	Shift_C(GET_REG_VAL(regs, Rm), SRType_LSR, imm, GET_APSR_C(regs), &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);	
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-236>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry) = Shift_C(R[m], SRType_ASR, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
*************************************/
void _asr_imm(uint32_t imm, uint32_t Rm, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{

	int carry = 0;
	uint32_t result;

	if(!ConditionPassed(0, regs)){
		return;
	}

	Shift_C(GET_REG_VAL(regs, Rm), SRType_ASR, imm, GET_APSR_C(regs), &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-224>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
	if d == 15 then
		ALUWritePC(result); // setflags is always FALSE here
	else
		R[d] = result;
		if setflags then
			APSR.N = result<31>;
			APSR.Z = IsZeroBit(result);
			APSR.C = carry;
			APSR.V = overflow;
*************************************/
void _add_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;
	uint32_t shifted;
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);
	
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	AddWithCarry(Rn_val, shifted, 0, &result, &carry, &overflow);
	
	// software won't distinguish PC and other registers like hardware did. Just disable setflags.
	if(Rd == 15){
		ALUWritePC(result, regs);
	}else{
		SET_REG_VAL(regs, Rd, result);
		if(setflags != 0){
			SET_APSR_N(regs, result);
			SET_APSR_Z(regs, result);
			SET_APSR_C(regs, carry);
			SET_APSR_V(regs, overflow);
		}
	}
}
	
/***********************************
<<ARMv7-M Architecture Reference Manual A7-227>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(SP, shifted, ��0��);
	if d == 15 then
		ALUWritePC(result); // setflags is always FALSE here
	else
		R[d] = result;
		if setflags then
			APSR.N = result<31>;
			APSR.Z = IsZeroBit(result);
			APSR.C = carry;
			APSR.V = overflow;
*************************************/
void _add_sp_reg(uint32_t Rm, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;
	uint32_t shifted;
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);
	
	uint32_t SP_val = GET_REG_VAL(regs, SP_INDEX);
	uint32_t result;
	AddWithCarry(SP_val, shifted, 0, &result, &carry, &overflow);
	
	// software won't distinguish PC and other registers like hardware did. Just disable setflags.
	if(Rd == 15){
		ALUWritePC(result, regs);
	}else{
		SET_REG_VAL(regs, Rd, result);
		if(setflags != 0){
			SET_APSR_N(regs, result);
			SET_APSR_Z(regs, result);
			SET_APSR_C(regs, carry);
			SET_APSR_V(regs, overflow);
		}
	}
}


/***********************************
<<ARMv7-M Architecture Reference Manual A7-498>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), '1');
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
*************************************/
void _sub_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;
	uint32_t shifted;
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	AddWithCarry(Rn_val, ~shifted, 1, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}

}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-222>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(R[n], imm32, ��0��);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
*************************************/
void _add_imm(uint32_t imm32, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	AddWithCarry(Rn_val, imm32, 0, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}

}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-496>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), ��1��);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
*************************************/
void _sub_imm(uint32_t imm32, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	AddWithCarry(Rn_val, ~imm32, 1, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-348>>
if ConditionPassed() then
	EncodingSpecificOperations();
	result = imm32;
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
*************************************/
void _mov_imm(uint32_t Rd, uint32_t imm32, uint32_t setflags, int carry, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t result = imm32;
	SET_REG_VAL(regs, Rd, result);
	if(setflags != 0){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-262>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), ��1��);
	APSR.N = result<31>;
	APSR.Z = IsZeroBit(result);
	APSR.C = carry;
	APSR.V = overflow;
*************************************/
void _cmp_imm(uint32_t imm32, uint32_t Rn, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t carry = GET_APSR_C(regs);
	uint32_t overflow;

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	AddWithCarry(Rn_val, ~imm32, 1, &result, &carry, &overflow);
	SET_APSR_N(regs, result);
	SET_APSR_Z(regs, result);
	SET_APSR_C(regs, carry);
	SET_APSR_V(regs, overflow);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-233>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = R[n] AND shifted;
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
	// APSR.V unchanged
*************************************/
void _and_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	int carry = GET_APSR_C(regs);

	uint32_t shifted;
	Shift_C(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted, &carry);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result = Rn_val & shifted;
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-273>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = R[n] EOR shifted;
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _eor_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	int carry = GET_APSR_C(regs);
	uint32_t shifted;
	Shift_C(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted, &carry);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result = Rn_val^shifted;
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-335>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shift_n = UInt(R[m]<7:0>);
	(result, carry) = Shift_C(R[n], SRType_LSL, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
	// APSR.V unchanged
**************************************/
void _lsl_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shift_n = GET_REG_VAL(regs, Rm) & 0x1F;
	uint32_t result;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	int carry = GET_APSR_C(regs);
	Shift_C(Rn_val, SRType_LSL, shift_n, carry, &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}


/***********************************
<<ARMv7-M Architecture Reference Manual A7-339>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shift_n = UInt(R[m]<7:0>);
	(result, carry) = Shift_C(R[n], SRType_LSR, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _lsr_reg(uint32_t Rm, uint32_t Rn ,uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shift_n = GET_REG_VAL(regs, Rm) & 0x1F;
	uint32_t result;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	int carry = GET_APSR_C(regs);
	Shift_C(Rn_val, SRType_LSR, shift_n, carry, &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-237>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shift_n = UInt(R[m]<7:0>);
	(result, carry) = Shift_C(R[n], SRType_ASR, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _asr_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shift_n = GET_REG_VAL(regs, Rm) & 0x1F;
	uint32_t result;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	int carry = GET_APSR_C(regs);
	Shift_C(Rn_val, SRType_ASR, shift_n, carry, &result, &carry);
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-219>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], shifted, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
**************************************/
void _adc_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	uint32_t carry = GET_APSR_C(regs);
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);
	uint32_t result;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t overflow;
	AddWithCarry(Rn_val, shifted, carry, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-419>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
**************************************/
void _sbc_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	uint32_t carry = GET_APSR_C(regs);
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result;
	uint32_t overflow;
	AddWithCarry(Rn_val, ~shifted, carry, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-408>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shift_n = UInt(R[m]<7:0>);
	(result, carry) = Shift_C(R[n], SRType_ROR, shift_n, APSR.C);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _ror_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t result;
	int carry = GET_APSR_C(regs);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t shift_n = GET_REG_VAL(regs, Rm) & 0x7F;

	Shift_C(Rn_val, SRType_ROR, shift_n, carry, &result, &carry);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-523>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = R[n] AND shifted;
	APSR.N = result<31>;
	APSR.Z = IsZeroBit(result);
	APSR.C = carry;
	// APSR.V unchanged
**************************************/
void _tst_reg(uint32_t Rm, uint32_t Rn, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	int carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift_C(Rm_val, shift_t, shift_n, carry, &shifted, &carry);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result = Rn_val & shifted;
	SET_APSR_N(regs, result);
	SET_APSR_Z(regs, result);
	SET_APSR_C(regs, carry);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-411>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(NOT(R[n]), imm32, ��1��);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
**************************************/
void _rsb_imm(uint32_t imm32, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	/* Warning: this function is not tested since MDK can't generate 16bit code for it */
	uint32_t result;
	uint32_t overflow;
	uint32_t carry = GET_APSR_C(regs);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	AddWithCarry(~Rn_val, imm32, carry, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-263>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), ��1��);
	APSR.N = result<31>;
	APSR.Z = IsZeroBit(result);
	APSR.C = carry;
	APSR.V = overflow;
**************************************/
void _cmp_reg(uint32_t Rm, uint32_t Rn, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	uint32_t carry = GET_APSR_C(regs);
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);

	uint32_t result;
	uint32_t overflow;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	AddWithCarry(Rn_val, ~shifted, 1, &result, &carry, &overflow);

	SET_APSR_N(regs, result);
	SET_APSR_Z(regs, result);
	SET_APSR_C(regs, carry);
	SET_APSR_V(regs, overflow);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-259>>
if ConditionPassed() then
	EncodingSpecificOperations();
	shifted = Shift(R[m], shift_t, shift_n, APSR.C);
	(result, carry, overflow) = AddWithCarry(R[n], shifted, ��0��);
	APSR.N = result<31>;
	APSR.Z = IsZeroBit(result);
	APSR.C = carry;
	APSR.V = overflow;
**************************************/
void _cmn_reg(uint32_t Rm, uint32_t Rn, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	uint32_t carry = GET_APSR_C(regs);
	Shift(GET_REG_VAL(regs, Rm), shift_t, shift_n, carry, &shifted);

	uint32_t result;
	uint32_t overflow;
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	AddWithCarry(Rn_val, shifted, 0, &result, &carry, &overflow);

	SET_APSR_N(regs, result);
	SET_APSR_Z(regs, result);
	SET_APSR_C(regs, carry);
	SET_APSR_V(regs, overflow);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-373>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = R[n] OR shifted;
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _orr_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	int32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift_C(Rm_val, shift_t, shift_n, carry, &shifted, &carry);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result = Rn_val | shifted;
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-359>>
if ConditionPassed() then
	EncodingSpecificOperations();
	operand1 = SInt(R[n]); // operand1 = UInt(R[n]) produces the same final results
	operand2 = SInt(R[m]); // operand2 = UInt(R[m]) produces the same final results
	result = operand1 * operand2;
	R[d] = result<31:0>;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		// APSR.C unchanged
		// APSR.V unchanged
**************************************/
void _mul_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	int32_t operand1 = (int32_t)GET_REG_VAL(regs, Rn);
	int32_t operand2 = (int32_t)GET_REG_VAL(regs, Rm);
	int32_t result = operand1 * operand2;

	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-245>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = R[n] AND NOT(shifted);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _bic_reg(uint32_t Rm, uint32_t Rn, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	int32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift_C(Rm_val, shift_t, shift_n, carry, &shifted, &carry);

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t result = Rn_val & (~shifted);
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-363>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
	result = NOT(shifted);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		// APSR.V unchanged
**************************************/
void _mvn_reg(uint32_t Rm, uint32_t Rd, SRType shift_t, uint32_t shift_n, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t shifted;
	int32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift_C(Rm_val, shift_t, shift_n, carry, &shifted, &carry);

	uint32_t result = ~shifted;
	SET_REG_VAL(regs, Rd, result);

	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-350>>
if ConditionPassed() then
	EncodingSpecificOperations();
	result = R[m];
	if d == 15 then
		ALUWritePC(result); // setflags is always FALSE here
	else
		R[d] = result;
		if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		// APSR.C unchanged
		// APSR.V unchanged
**************************************/
void _mov_reg(uint32_t Rm, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t result = Rm_val;
	if(Rd == 15){
		ALUWritePC(result, regs);
	}else{
		SET_REG_VAL(regs, Rd, result);
		if(setflags){
			SET_APSR_N(regs, result);
			SET_APSR_Z(regs, result);
		}
	}
}


/***********************************
<<ARMv7-M Architecture Reference Manual A7-250>>
if ConditionPassed() then
	EncodingSpecificOperations();
	BXWritePC(R[m]);
**************************************/
void _bx(uint32_t Rm, armv7m_reg_t* regs, armv7m_state* state)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	BXWritePC(Rm_val, regs, state);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-249>>
if ConditionPassed() then
	EncodingSpecificOperations();
	target = R[m];
	next_instr_addr = PC - 2;
	LR = next_instr_addr<31:1> : ��1��;
	BLXWritePC(target);
**************************************/
void _blx(uint32_t Rm, armv7m_reg_t* regs, armv7m_state* state)
{
	/* need to be fix for not T1 and T3*/
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t target = GET_REG_VAL(regs, Rm);
	uint32_t PC_val = GET_REG_VAL(regs, PC_INDEX);
	uint32_t next_instr_addr = PC_val - 2;
	uint32_t LR_val = next_instr_addr | 0x1ul;
	SET_REG_VAL(regs, LR_INDEX, LR_val);
	BXWritePC(target, regs, state);
}


/***********************************
<<ARMv7-M Architecture Reference Manual A7-289>>
if ConditionPassed() then
	EncodingSpecificOperations();
	base = Align(PC,4);
	address = if add then (base + imm32) else (base - imm32);
	data = MemU[address,4];
	if t == 15 then
		if address<1:0> == ��00�� then LoadWritePC(data); else UNPREDICTABLE;
	else
		R[t] = data;
**************************************/
void _ldr_literal(uint32_t imm32, uint32_t Rt, bool_t add, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t base = GET_REG_VAL(regs, PC_INDEX);
	base = DOWN_ALIGN(base, 2);
	uint32_t address = add ? base+imm32 : base-imm32;

	uint32_t data;
	MemU(address, 4, (uint8_t *)&data, MEM_READ, regs, state, memory_map);
	if(Rt == 15){
		if((address & 0x3ul) == 0){
			LoadWritePC(data, regs, state);
		}else{
			LOG(LOG_WARN, "_ldr_literal UNPREDICTABLE\n");
			return;
		}
	}else{
		SET_REG_VAL(regs, Rt, data);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-475>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	address = R[n] + offset;
	data = R[t];
	MemU[address,4] = data;
**************************************/
void _str_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	int32_t carry = GET_APSR_C(regs);
	uint32_t offset;
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t address = Rn_val + offset; 
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 4, (uint8_t*)&data, MEM_WRITE, regs, state, memory_map);
}


/***********************************
<<ARMv7-M Architecture Reference Manual A7-491>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	address = R[n] + offset;
	MemU[address,2] = R[t]<15:0>;
**************************************/
void _strh_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	int32_t carry = GET_APSR_C(regs);
	uint32_t offset;
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t address = Rn_val + offset; 
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 2, (uint8_t*)&data, MEM_WRITE, regs, state, memory_map);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-479>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	address = R[n] + offset;
	MemU[address,1] = R[t]<7:0>;
**************************************/
void _strb_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	int32_t carry = GET_APSR_C(regs);
	uint32_t offset;
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t address = Rn_val + offset; 
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 1, (uint8_t*)&data, MEM_WRITE, regs, state, memory_map);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-321>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	offset_addr = if add then (R[n] + offset) else (R[n] - offset);
	address = if index then offset_addr else R[n];
	R[t] = SignExtend(MemU[address,1], 32);
**************************************/
void _ldrsb_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, bool_t add, bool_t index, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t offset;
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	int32_t carry = GET_APSR_C(regs);
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+offset : Rn_val-offset;
	uint32_t address = index ? offset_addr : Rn_val;
	int8_t data;
	MemU(address, 1, (uint8_t*)&data, MEM_READ, regs, state, memory_map);
	SET_REG_VAL(regs, Rt, (int32_t)data);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-291>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	offset_addr = if add then (R[n] + offset) else (R[n] - offset);
	address = if index then offset_addr else R[n];
	data = MemU[address,4];
	if wback then R[n] = offset_addr;
	if t == 15 then
		if address<1:0> == ��00�� then LoadWritePC(data); else UNPREDICTABLE;
	else
		R[t] = data;
**************************************/
void _ldr_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, bool_t add, bool_t index, bool_t wback, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t offset;
	uint32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+offset : Rn_val-offset;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data;
	MemU(address, 4, (uint8_t *)&data, MEM_READ, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
	if(Rt == 15){
		if((address & 0x3ul) == 0){
			LoadWritePC(data, regs, state);
		}else{
			LOG(LOG_WARN, "_ldr_reg UNPREDICTABLE\n");
		}
	}else{
		SET_REG_VAL(regs, Rt, data);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-313>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	offset_addr = if add then (R[n] + offset) else (R[n] - offset);
	address = if index then offset_addr else R[n];
	data = MemU[address,2];
	if wback then R[n] = offset_addr;
	R[t] = ZeroExtend(data, 32);
**************************************/
void _ldrh_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, bool_t add, bool_t index, bool_t wback, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t offset;
	uint32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+offset : Rn_val-offset;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = 0;
	MemU(address, 2, (uint8_t *)&data, MEM_READ, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
	SET_REG_VAL(regs, Rt, data);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-297>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	offset_addr = if add then (R[n] + offset) else (R[n] - offset);
	address = if index then offset_addr else R[n];
	R[t] = ZeroExtend(MemU[address,1],32);
**************************************/
void _ldrb_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, bool_t add, bool_t index, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t offset;
	uint32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+offset : Rn_val-offset;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = 0;
	MemU(address, 1, (uint8_t *)&data, MEM_READ, regs, state, memory_map);
	SET_REG_VAL(regs, Rt, data);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-329>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset = Shift(R[m], shift_t, shift_n, APSR.C);
	offset_addr = if add then (R[n] + offset) else (R[n] - offset);
	address = if index then offset_addr else R[n];
	data = MemU[address,2];
	if wback then R[n] = offset_addr;
	R[t] = SignExtend(data, 32);
**************************************/
void _ldrsh_reg(uint32_t Rm, uint32_t Rn, uint32_t Rt, bool_t add, bool_t index, bool_t wback, SRType shift_t, uint32_t shift_n, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t offset;
	uint32_t carry = GET_APSR_C(regs);
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	Shift(Rm_val, shift_t, shift_n, carry, &offset);
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+offset : Rn_val-offset;
	uint32_t address = index ? offset_addr : Rn_val;
	int16_t data;
	MemU(address, 2, (uint8_t *)&data, MEM_READ, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
	SET_REG_VAL(regs, Rt, (int32_t)data);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-473>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	MemU[address,4] = R[t];
	if wback then R[n] = offset_addr;
**************************************/
void _str_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 4, (uint8_t *)&data, MEM_WRITE, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-287>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	data = MemU[address,4];
	if wback then R[n] = offset_addr;
	if t == 15 then
		if address<1:0> == ��00�� then LoadWritePC(data); else UNPREDICTABLE;
	else
		R[t] = data;
**************************************/
void _ldr_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data;
	MemU(address, 4, (uint8_t*)&data, MEM_READ, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
	if(Rt == 15){
		if((address & 0x3ul) == 0){
			LoadWritePC(data, regs, state);
		}else{
			LOG(LOG_WARN, "_str_imm UNPREDICTABLE\n");
		}
	}else{
		SET_REG_VAL(regs, Rt, data);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-477>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	MemU[address,1] = R[t]<7:0>;
	if wback then R[n] = offset_addr;
**************************************/
void _strb_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 1, (uint8_t *)&data, MEM_WRITE, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-293>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	R[t] = ZeroExtend(MemU[address,1], 32);
	if wback then R[n] = offset_addr;
**************************************/
void _ldrb_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = 0;
	MemU(address, 1, (uint8_t*)&data, MEM_READ, regs, state, memory_map);
	SET_REG_VAL(regs, Rt, data);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-489>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	MemU[address,2] = R[t]<15:0>;
	if wback then R[n] = offset_addr;
**************************************/
void _strh_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = GET_REG_VAL(regs, Rt);
	MemU(address, 2, (uint8_t *)&data, MEM_WRITE, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-309>>
if ConditionPassed() then
	EncodingSpecificOperations();
	offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
	address = if index then offset_addr else R[n];
	data = MemU[address,2];
	if wback then R[n] = offset_addr;
	R[t] = ZeroExtend(data, 32);
**************************************/
void _ldrh_imm(uint32_t imm32, uint32_t Rn, uint32_t Rt,bool_t add, bool_t index, bool_t wback, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory_map)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t offset_addr = add ? Rn_val+imm32 : Rn_val-imm32;
	uint32_t address = index ? offset_addr : Rn_val;
	uint32_t data = 0;
	MemU(address, 2, (uint8_t*)&data, MEM_READ, regs, state, memory_map);
	if(wback){
		SET_REG_VAL(regs, Rn, offset_addr);
	}
	SET_REG_VAL(regs, Rt, data);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-229>>
if ConditionPassed() then
	EncodingSpecificOperations();
	result = if add then (Align(PC,4) + imm32) else (Align(PC,4) - imm32);
	R[d] = result;
**************************************/
void _adr(uint32_t imm32, uint32_t Rd, bool_t add, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t PC_val = GET_REG_VAL(regs, PC_INDEX);
	uint32_t result = add ? DOWN_ALIGN(PC_val, 2)+imm32 : DOWN_ALIGN(PC_val, 2)-imm32;
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-225>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(SP, imm32, ��0��);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
**************************************/
void _add_sp_imm(uint32_t imm32, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t result, carry, overflow;
	uint32_t SP_val = GET_REG_VAL(regs, SP_INDEX);
	AddWithCarry(SP_val, imm32, 0, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-499>>
if ConditionPassed() then
	EncodingSpecificOperations();
	(result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ��1��);
	R[d] = result;
	if setflags then
		APSR.N = result<31>;
		APSR.Z = IsZeroBit(result);
		APSR.C = carry;
		APSR.V = overflow;
**************************************/
void _sub_sp_imm(uint32_t imm32, uint32_t Rd, uint32_t setflags, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	uint32_t result, carry, overflow;
	uint32_t SP_val = GET_REG_VAL(regs, SP_INDEX);
	AddWithCarry(SP_val, ~imm32, 1, &result, &carry, &overflow);
	SET_REG_VAL(regs, Rd, result);
	if(setflags){
		SET_APSR_N(regs, result);
		SET_APSR_Z(regs, result);
		SET_APSR_C(regs, carry);
		SET_APSR_V(regs, overflow);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-251>>
EncodingSpecificOperations();
	if nonzero ^ IsZero(R[n]) then
	BranchWritePC(PC + imm32);
**************************************/
void _cbnz_cbz(uint32_t imm32, uint32_t Rn, uint32_t nonzero, armv7m_reg_t* regs)
{
	uint32_t Rn_val = GET_REG_VAL(regs, Rn);
	uint32_t PC_val = GET_REG_VAL(regs, PC_INDEX);
	if(nonzero ^ (Rn_val == 0)){
		BranchWritePC(PC_val + imm32, regs);
	}
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-515>>
if ConditionPassed() then
	EncodingSpecificOperations();
	rotated = ROR(R[m], rotation);
	R[d] = SignExtend(rotated<15:0>, 32);
**************************************/
void _sxth(uint32_t Rm, uint32_t Rd, uint32_t rotation, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t rotated = Rm_val >> rotation;
	uint32_t result = (int32_t)((int16_t)rotated);
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-513>>
if ConditionPassed() then
EncodingSpecificOperations();
	rotated = ROR(R[m], rotation);
	R[d] = SignExtend(rotated<7:0>, 32);
**************************************/
void _sxtb(uint32_t Rm, uint32_t Rd, uint32_t rotation, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t rotated = Rm_val >> rotation;
	uint32_t result = (int32_t)((int8_t)rotated);
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-563>>
if ConditionPassed() then
	EncodingSpecificOperations();
	rotated = ROR(R[m], rotation);
	R[d] = ZeroExtend(rotated<15:0>, 32);
**************************************/
void _uxth(uint32_t Rm, uint32_t Rd, uint32_t rotation, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t rotated = Rm_val >> rotation;
	uint32_t result = ((uint16_t)rotated);
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-559>>
if ConditionPassed() then
	EncodingSpecificOperations();
	rotated = ROR(R[m], rotation);
	R[d] = ZeroExtend(rotated<7:0>, 32);
**************************************/
void _uxtb(uint32_t Rm, uint32_t Rd, uint32_t rotation, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t rotated = Rm_val >> rotation;
	uint32_t result = (uint8_t)rotated;
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-389>>
if ConditionPassed() then
	EncodingSpecificOperations();
	address = SP - 4*BitCount(registers);

	for i = 0 to 14
		if registers<i> == ��1�� then
			MemA[address,4] = R[i];
			address = address + 4;

	SP = SP - 4*BitCount(registers);
**************************************/
void _push(uint32_t registers, uint32_t bitcount, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t SP_val = GET_REG_VAL(regs, SP_INDEX);
	uint32_t address = SP_val - (bitcount << 2);

	uint32_t data;
	int i;
	for(i = 0; i < 15; i++){
		if(registers & (1ul << i)){
			data = GET_REG_VAL(regs, i);
			MemA(address, 4, (uint8_t*)&data, MEM_WRITE, regs, state, memory);
			address += 4;
		}
	}

	SP_val -= bitcount << 2;
	SET_REG_VAL(regs, SP_INDEX, SP_val);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-402>>
if ConditionPassed() then
	EncodingSpecificOperations();
	bits(32) result;
	result<31:24> = R[m]<7:0>;
	result<23:16> = R[m]<15:8>;
	result<15:8> = R[m]<23:16>;
	result<7:0> = R[m]<31:24>;
	R[d] = result;
**************************************/
void _rev(uint32_t Rm, uint32_t Rd, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t result;
	result = ((Rm_val&0x0000FFFF) << 16) | ((Rm_val&0xFFFF0000) >> 16);
	result = ((result&0x00FF00FF) << 8) | ((result&0xFF00FF00) >> 8);
	//result = ((result&0x0F0F0F0F) << 4) | ((result&0xF0F0F0F0) >> 4);
	//result = ((result&0x33333333) << 2) | ((result&0xCCCCCCCC) >> 2);
	//result = ((result&0x55555555) << 1) | ((result&0xAAAAAAAA) >> 1);
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-403>>
if ConditionPassed() then
	EncodingSpecificOperations();
	bits(32) result;
	result<31:24> = R[m]<23:16>;
	result<23:16> = R[m]<31:24>;
	result<15:8> = R[m]<7:0>;
	result<7:0> = R[m]<15:8>;
	R[d] = result;
**************************************/
void _rev16(uint32_t Rm, uint32_t Rd, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t result;
	result = ((Rm_val&0x00FF00FF) << 8) | ((Rm_val&0xFF00FF00) >> 8);
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-404>>
if ConditionPassed() then
	EncodingSpecificOperations();
	bits(32) result;
	result<31:8> = SignExtend(R[m]<7:0>, 24);
	result<7:0> = R[m]<15:8>;
	R[d] = result;
**************************************/
void _revsh(uint32_t Rm, uint32_t Rd, armv7m_reg_t* regs)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t Rm_val = GET_REG_VAL(regs, Rm);
	uint32_t result;
	result = ((Rm_val&0x000000FF) << 8) | ((Rm_val&0x0000FF00) >> 8);
	result = (int16_t)result;
	SET_REG_VAL(regs, Rd, result);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-387>>
if ConditionPassed() then
	EncodingSpecificOperations();
	address = SP;

	for i = 0 to 14
		if registers<i> == ��1�� then
			R[i] = MemA[address,4]; address = address + 4;
	if registers<15> == ��1�� then
		LoadWritePC(MemA[address,4]);

	SP = SP + 4*BitCount(registers);
**************************************/
void _pop(uint32_t registers, uint32_t bitcount, armv7m_reg_t* regs, armv7m_state* state, memory_map_t* memory)
{
	if(!ConditionPassed(0, regs)){
		return;
	}

	// EncodingSpecificOperations();
	uint32_t SP_val = GET_REG_VAL(regs, SP_INDEX);
	uint32_t address = SP_val;

	uint32_t data;
	int i;
	for(i = 0; i < 15; i++){
		if(registers & (1ul << i)){
			MemA(address, 4, (uint8_t*)&data, MEM_READ, regs, state, memory);
			SET_REG_VAL(regs, i, data);
			address += 4;
		}
	}
	if(registers & (1ul << 15)){
		MemA(address, 4, (uint8_t*)&data, MEM_READ, regs, state, memory);
		LoadWritePC(data, regs, state);
	}

	SP_val += bitcount << 2;
	SET_REG_VAL(regs, SP_INDEX, SP_val);
}

/***********************************
<<ARMv7-M Architecture Reference Manual A7-387>>
EncodingSpecificOperations();
ITSTATE.IT<7:0> = firstcond:mask;
**************************************/
void _it(uint32_t firstcond, uint32_t mask, armv7m_reg_t* regs, armv7m_state* state)
{
	// EncodingSpecificOperations();
	uint8_t value = (firstcond << 4) | mask;
	state->excuting_IT = TRUE;
	SET_ITSTATE(regs, value);
}