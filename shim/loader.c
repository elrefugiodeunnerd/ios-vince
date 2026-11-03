/*
 * Vince iBoot-like loader for XNU Mach-O payload on Snapdragon 625 (MSM8953)
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_4K				0x1000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static uint64_t rd_cntfrq(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static uint64_t rd_cntvct(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static void delay_ms(uint64_t ms)
{
	uint64_t t0 = rd_cntvct(), d = rd_cntfrq() / 1000 * ms;
	while (rd_cntvct() - t0 < d)
		__asm__ volatile("wfi");
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want1[] = "SWEETXNU";
	static const char want2[] = "VINCEXNU";
	int ok1 = 1, ok2 = 1;
	for (int i = 0; i < 8; i++) {
		if (m[i] != want1[i]) ok1 = 0;
		if (m[i] != want2[i]) ok2 = 0;
	}
	return ok1 || ok2;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no VINCEXNU/SWEETXNU payload in ramdisk";
	if (p->version != 1 || (p->adt_off & 7) || (rd_start & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;

	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	con_str("LOADER",  "Copying kernel segments...");
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}

	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_4K - 1) & ~(PAGE_4K - 1);
	uintptr_t top = ba_pa + PAGE_4K;

	/* the scaled display (the mia1 shim's fbscale trick): fbw=750 fbh=1624 reprograms
	 * the VIG0 pipe to fetch the splash memory at that size, XRGB8888, and stretch it
	 * to the 1080x2280 panel with its QSEED scaler -- iOS then draws 2.25 times fewer
	 * pixels and the processor does no scaling at all */
	static uint32_t smoke_w, smoke_h;
	int scaled = 0;
	{
		const char *cmd = p->cmdline;
		uint32_t fbw = 0, fbh = 0;
		for (const char *a = cmd; *a; ) {
			const char *sp = a;
			while (*sp && *sp != ' ') sp++;
			unsigned long len = (unsigned long)(sp - a);
			if (len > 4 && a[0] == 'f' && a[1] == 'b' && a[2] == 'w' && a[3] == '=') {
				fbw = 0;
				for (unsigned long k = 4; k < len; k++) fbw = fbw * 10 + (a[k] - '0');
			} else if (len > 4 && a[0] == 'f' && a[1] == 'b' && a[2] == 'h' && a[3] == '=') {
				fbh = 0;
				for (unsigned long k = 4; k < len; k++) fbh = fbh * 10 + (a[k] - '0');
			}
			if (!sp) break;
			a = sp + 1;
		}
		if (fbw >= 480 && fbh >= 960 && fbw < FB_W && fbh < FB_H && (fbw & 1) == 0 && (fbh & 1) == 0) {
			con_switch_scaled(fbw, fbh);
			con_clear(0x00102040);
			con_str("DISPLAY", "VIG0 scaled mode: iOS draws fbw x fbh, QSEED to the panel");
			smoke_w = fbw;
			smoke_h = fbh;
			scaled = 1;
		}
	}

	zero64(ba_pa, PAGE_4K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	if (scaled) {
		/* the VIG0 pipe fetches XRGB8888 at fbw x fbh and scales it to the panel:
		 * XNU renders straight into the fetch buffer, the MDP does the rest */
		ba->Video.v_rowBytes = smoke_w * 4;
		ba->Video.v_width = smoke_w;
		ba->Video.v_height = smoke_h;
		ba->Video.v_depth = 32;
	} else {
		ba->Video.v_rowBytes = FB_STRIDE;
		ba->Video.v_width = FB_W;
		ba->Video.v_height = FB_H;
		ba->Video.v_depth = 24;
	}
	ba->deviceTreeP = adt_pa - pa_of_va_base;
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("ENTRY",   entry_pa);
	con_hex("BOOTARG", ba_pa);

	/*
	 * Pre-handover diagnostics. Everything here runs before XNU and can
	 * see (a) what ABL itself thinks DRAM is, (b) whether the pieces we
	 * copied actually landed in DRAM, and (c) whether writes into DRAM
	 * disappear the way they would if the physical address were wrong.
	 *
	 * We read back with D-cache disabled but MMU still on, so every probe
	 * read goes straight to physical DRAM rather than a dirty cache line.
	 */
	con_str("PROBE", "flushing cached copy to DRAM...");
	for (uintptr_t a = p->phys_base; a < top; a += 64)
		__asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");

	/* (c) write probe just past boot_args (XNU does not start there yet) */
	uintptr_t probe_pa = top;
	volatile uint64_t *pw = (volatile uint64_t *)probe_pa;
	pw[0] = 0xdeadbeefcafef00dULL;
	pw[1] = 0x1badb00213571357ULL;
	__asm__ volatile("dc cvac, %0" : : "r"(probe_pa) : "memory");
	__asm__ volatile("dc cvac, %0" : : "r"(probe_pa + 8) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");

	uint64_t sctlr_probe;
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_probe));
	sctlr_probe &= ~(1ULL << 2);	/* D-cache off, MMU still on */
	__asm__ volatile("msr sctlr_el1, %0; isb" : : "r"(sctlr_probe) : "memory");

	uint64_t rb0 = pw[0], rb1 = pw[1];
	con_hex("PROBE0", rb0);
	con_hex("PROBE1", rb1);

	/* (b1) what did we actually end up with at the XNU entry point? */
	volatile const uint32_t *ep = (volatile const uint32_t *)entry_pa;
	for (int i = 0; i < 8; i += 2) {
		uint64_t v = ((uint64_t)ep[i] << 32) | ep[i + 1];
		con_hex(i == 0 ? "ENTR00" : (i == 2 ? "ENTR04" : (i == 4 ? "ENTR08" : "ENTR0C")), v);
	}

	/* (b2) and in the ADT page */
	volatile const uint32_t *ad = (volatile const uint32_t *)adt_pa;
	uint32_t adsum = 0;
	for (uint32_t i = 0; i < (uint32_t)(p->adt_size / 4); i++)
		adsum += ad[i];
	con_hex("ADTSUM", adsum);

	if (rb0 == 0xdeadbeefcafef00dULL && rb1 == 0x1badb00213571357ULL)
		con_str_color("PROBE", "DRAM readback OK, copying reached DRAM.", GREEN);
	else {
		con_str_color("PROBE", "DRAM READBACK FAILED - halting, photo?", RED);
		while (1)
			__asm__ volatile("wfi");
	}

	/* (a) the bootloader's own view of DRAM and its reservations */
	{
		static uint64_t membuf[2 + 15 * 2];
		fdt_dump_memory(dtb, membuf, 64);
	}

	con_str("HANDOVER", "Flushing cache & entering XNU...");

	/* 5 second grace so the reader can actually capture the probes. */
	delay_ms(5000);

	/* Disable MMU and D-cache so XNU enters with clean flat physical addressing */
	uint64_t sctlr;
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr &= ~(1ULL << 0); /* SCTLR_M = 0 */
	sctlr &= ~(1ULL << 2); /* SCTLR_C = 0 */
	__asm__ volatile("msr sctlr_el1, %0; isb" : : "r"(sctlr) : "memory");

	/* Invalidate TLB and I-cache */
	__asm__ volatile(
		"tlbi vmalle1\n"
		"dsb sy\n"
		"ic iallu\n"
		"dsb sy\n"
		"isb\n"
		::: "memory"
	);

	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
