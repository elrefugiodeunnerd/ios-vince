/* MMIO oracle: voltage ACK precedes SRAM timing/PLL; never expose calibration. */
static uint32_t gcc_regs[0x60000 / 4], acc_value;
static unsigned reads, writes, sleeps, votes, arg_mhz, arg_test, fault;
static unsigned rcg_updates, reset_delay, calibration, slew_delay;
static bool cx_ack, lock_ready;

#define R(off) gcc_regs[(off) / 4]
enum {
	NONE, MAP_FAIL, RPM_FAIL, ACC_FAIL, PLL_LOCK_FAIL, PLL_UPDATE_FAIL,
	PLL_RELOCK_FAIL, PLL_READBACK_FAIL, RCG_FAIL, RCG_ROLLBACK_FAIL, BRANCH_FAIL,
};

static void
fresh(void)
{
	memset(gcc_regs, 0, sizeof(gcc_regs));
	reads = writes = sleeps = votes = rcg_updates = reset_delay = calibration = slew_delay = 0;
	cx_ack = lock_ready = false;
	arg_mhz = 650; arg_test = 1; last_step = 11; skip_steps = 0; fault = NONE;
	acc_value = 0xa5; /* unrelated memory-acc bits must survive */
	R(GCC_GFX_PWRSWITCH_CTRL) = 0x80000038;
	R(GCC_GFX_PWRSWITCH_STATUS) = 3;
	R(GCC_GFX3D_CFG_RCGR) = 0x507;
	R(GCC_GFX3D_CMD_RCGR) = 0x6002;
	R(GCC_OXILI_CX_GDSCR) = R(GCC_OXILI_GX_GDSCR) = 0x80000000;
	R(GCC_OXILI_GFX3D_CBCR) = 1;
	R(DG_PLL_USER) = 0x10000000; /* unrelated PLL bits must survive */
}

static uint32_t
reg_read(vm_offset_t base, uint32_t off)
{
	reads++;
	if (base == 2) { assert(off == 0x15c); return acc_value; }
	assert(base == 1 && !(off & 3) && off < sizeof(gcc_regs));
	return R(off);
}

static void
reg_write(vm_offset_t base, uint32_t off, uint32_t value)
{
	writes++;
	assert(cx_ack); /* neither memory timing nor a clock write before power ACK */
	if (base == 2) {
		assert(off == 0x15c && value == 0xa4);
		if (fault != ACC_FAIL) { acc_value = value; }
		return;
	}
	assert(base == 1 && !(off & 3) && off < sizeof(gcc_regs));
	assert(acc_value == 0xa4);
	if (off >= 0x22000 && off <= 0x22020) {
		assert(R(GCC_GFX3D_CFG_RCGR) == 0x507); /* downstream remains on GPLL0 */
		if (off == DG_PLL_MODE) {
			if ((value & 7) == 2) { reset_delay = sleeps; }
			if ((value & 7) == 6 && !(value & DG_UPDATE) && !calibration) {
				assert(sleeps - reset_delay >= 5);
				/* 1500 MHz: 78 + 1/8, alpha 0x2000000000 */
				assert(R(DG_PLL_L) == 78 && R(DG_PLL_ALPHA) == 0 && R(DG_PLL_ALPHA_HI) == 0x20);
				assert(R(DG_PLL_CONFIG) == 0x4001055b);
				assert(R(DG_PLL_USER) == 0x11000101);
				calibration++;
				if (fault != PLL_LOCK_FAIL) { value |= DG_LOCK; }
			}
			if (value & DG_UPDATE) {
				assert(calibration && !(value & 1));
				assert(R(DG_PLL_L) == 67 && R(DG_PLL_ALPHA) == 0x55555600 && R(DG_PLL_ALPHA_HI) == 0xb5);
				if (fault != PLL_UPDATE_FAIL) { value &= ~DG_UPDATE; }
				value &= ~DG_LOCK;
				lock_ready = true;
				slew_delay = sleeps;
			}
			if (value & 1) {
				assert(value & DG_LOCK);
				assert(sleeps - slew_delay >= 1);
				if (fault == PLL_READBACK_FAIL) { R(DG_PLL_L) = 66; }
			}
		}
	} else if (off == GCC_GFX3D_CFG_RCGR) {
		assert(value == 0x201 || value == 0x507);
		if (value == 0x201) { assert(dg_pll_matches()); }
	} else if (off == GCC_GFX3D_CMD_RCGR) {
		assert((value & ~1u) == (R(off) & ~1u)); /* root and retention unchanged */
		rcg_updates++;
		bool fails = fault == RCG_ROLLBACK_FAIL || (fault == RCG_FAIL && rcg_updates == 1);
		if (!fails) { value &= ~1u; }
		R(GCC_OXILI_GFX3D_CBCR) = fault == BRANCH_FAIL && rcg_updates == 1 ? 0x80000001 : 1;
	} else { assert(!"unexpected register write"); }
	R(off) = value;
}

static void
delay(unsigned us)
{
	sleeps += us;
	assert(sleeps < 5000); /* all failure paths bounded */
	if (lock_ready && sleeps > slew_delay && fault != PLL_RELOCK_FAIL) { R(DG_PLL_MODE) |= DG_LOCK; }
}

static bool
PE_parse_boot_argn(const char *name, void *out, unsigned size)
{
	assert(size == sizeof(uint32_t));
	*(uint32_t *)out = !strcmp(name, "daisygpu_mhz") ? arg_mhz : arg_test;
	return true;
}

static vm_offset_t
ml_io_map(vm_offset_t pa, unsigned size)
{
	assert(pa == 0x01944000 && size == 0x1000);
	return fault == MAP_FAIL ? 0 : 2;
}

int
daisy_gpu_cx_turbo(void)
{
	votes++;
	cx_ack = fault != RPM_FAIL;
	return cx_ack ? 0 : -7;
}

int
main(void)
{
	fresh(); assert(daisy_gpu_clock_start());
	assert(votes == 1 && rcg_updates == 1 && dg_pll_matches());
	assert(R(GCC_GFX3D_CFG_RCGR) == 0x201 && R(GCC_GFX3D_CMD_RCGR) == 0x6002);
	/* No option/unsupported option/partial startup must never access hardware. */
	for (unsigned n = 0; n < 5; n++) {
		fresh();
		if (n == 0) { arg_mhz = 0; }
		if (n == 1) { arg_mhz = 700; }
		if (n == 2) { arg_test = 0; }
		if (n == 3) { last_step = 5; }
		if (n == 4) { skip_steps = 8; }
		assert(daisy_gpu_clock_start() && !reads && !writes && !votes);
	}
	for (unsigned n = 0; n < 8; n++) {
		fresh();
		if (n == 0) { R(GCC_GFX_PWRSWITCH_CTRL) |= 1u << 9; }
		if (n == 1) { R(GCC_GFX_PWRSWITCH_CTRL) |= 1u << 8; }
		if (n == 2) { R(GCC_GFX_PWRSWITCH_STATUS) = 1; }
		if (n == 3) { R(GCC_GFX3D_CFG_RCGR) = 1; }
		if (n == 4) { R(GCC_OXILI_CX_GDSCR) = 0; }
		if (n == 5) { R(GCC_OXILI_GX_GDSCR) = 0; }
		if (n == 6) { R(GCC_GFX3D_CMD_RCGR) |= 1; }
		if (n == 7) { R(GCC_GFX_PWRSWITCH_CTRL) |= 1u << 30; }
		assert(daisy_gpu_clock_start() && !writes && !votes);
	}
	for (unsigned f = MAP_FAIL; f <= BRANCH_FAIL; f++) {
		fresh(); fault = f;
		assert(daisy_gpu_clock_start() == (f != RCG_ROLLBACK_FAIL));
		if (f == MAP_FAIL || f == RPM_FAIL) { assert(!writes && !rcg_updates); }
		assert(R(GCC_GFX3D_CFG_RCGR) == 0x507);
		if (f >= RCG_FAIL) { assert(rcg_updates == 2); }
	}
	/* Do not take over an enabled, foreign-frequency or FSM-owned PLL. */
	const uint32_t modes[] = {7, 0x80000007, 1u << 20, DG_UPDATE};
	for (unsigned n = 0; n < sizeof(modes) / sizeof(modes[0]); n++) {
		fresh(); R(DG_PLL_MODE) = modes[n];
		assert(daisy_gpu_clock_start() && !calibration && !rcg_updates);
		assert(R(DG_PLL_MODE) == modes[n]);
	}
	/* An already-locked matching handoff can be used without reprogramming. */
	fresh(); R(DG_PLL_MODE) = 0x80000007; R(DG_PLL_USER) = 0x01000101;
	R(DG_PLL_L) = 67; R(DG_PLL_ALPHA) = 0x55555600; R(DG_PLL_ALPHA_HI) = 0xb5;
	assert(daisy_gpu_clock_start() && rcg_updates == 1 && !calibration);
	puts("PASS: Daisy 650 MHz power ordering, calibration/slew, opt-in guards and rollback faults");
	return 0;
}
