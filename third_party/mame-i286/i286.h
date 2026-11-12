// license:BSD-3-Clause
// copyright-holders:Carl
#pragma once

#include "i86.h"

namespace boothush_i286 {

class i80286_cpu_device final : public i8086_common_cpu_device
{
public:
	explicit i80286_cpu_device(Bus &bus);
	void reset();
	uint32_t run(uint32_t cycles);
	uint32_t pc() const { return (m_base[CS] + m_ip) & m_amask; }
	uint16_t cs() const { return m_sregs[CS]; }
	uint16_t ip() const { return m_ip; }
	bool halted() const { return m_halt; }
	bool interruptible() const { return m_IF && !m_no_interrupt; }
	void set_irq(bool asserted);
	bool shutdown() const { return m_shutdown; }
	uint32_t fault_pc() const { return m_fault_pc; }
	int last_exception() const { return m_last_exception; }
	void set_a20(bool enabled) { m_amask = enabled ? 0xffffff : 0xefffff; }

private:
	void execute_run();

	virtual void interrupt(int int_num, int trap = 1) override { if(trap) throw TRAP(int_num, (uint16_t)-1); else interrupt_descriptor(int_num, 0, 0); }
	virtual uint8_t read_port_byte(uint16_t port) override;
	virtual uint16_t read_port_word(uint16_t port) override;
	virtual void write_port_byte(uint16_t port, uint8_t data) override;
	virtual void write_port_word(uint16_t port, uint16_t data) override;

	// Executing instructions
	virtual uint8_t fetch() override;
	virtual uint32_t calc_addr(int seg, uint16_t offset, int size, int op, bool override = true) override;

	void check_permission(uint8_t check_seg, uint32_t offset, uint16_t size, int operation);
	void code_descriptor(uint16_t selector, uint16_t offset, int gate);
	void data_descriptor(int reg, uint16_t selector);
	void data_descriptor(int reg, uint16_t selector, int cpl, uint32_t trap, uint16_t offset = 0, int size = 0);
	uint16_t far_return(int iret, int bytes);
	void interrupt_descriptor(int number, int hwint, int error);
	void load_flags(uint16_t flags, int cpl);
	void pop_seg(int reg);
	uint32_t selector_address(uint16_t sel);
	void switch_task(uint16_t ntask, int type);
	void trap(uint32_t error);
	int verify(uint16_t selector, int operation, uint8_t rights, bool valid);
	uint32_t update_pc() { return m_pc = m_base[CS] + m_ip; }

	int m_trap_level;
	uint16_t m_msw;
	uint32_t m_base[4];
	uint16_t m_limit[4];
	uint8_t m_rights[4];
	bool m_valid[4];

	struct {
		uint32_t base;
		uint16_t limit;
	} m_gdtr, m_idtr;
	struct {
		uint16_t sel;
		uint32_t base;
		uint16_t limit;
		uint8_t rights;
	} m_ldtr, m_tr;

	uint32_t TRAP(uint16_t fault, uint16_t code)  { return ((((uint32_t)fault&0xffff)<<16)|(code&0xffff)); }

	static const uint8_t m_i80286_timing[200];
	static const uint8_t m_i80286_ea_timing[200];

	enum {
		FAULT_DE = 0,
		FAULT_DB,
		NMI,
		FAULT_BP,
		FAULT_OF,
		FAULT_BR,
		FAULT_UD,
		FAULT_NM,
		FAULT_DF,
		FAULT_MP,
		FAULT_TS,
		FAULT_NP,
		FAULT_SS,
		FAULT_GP
	};

	bool m_shutdown;
	uint32_t m_fault_pc;
	int m_last_exception;
};

} // namespace boothush_i286
