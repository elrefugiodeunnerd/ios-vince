/*
 * Text console on the ABL splash framebuffer, dual mode:
 *   24-bpp: 1080 x 2280 RGB888, stride 3240 (the ABL splash's own layout)
 *   32-bpp: fbw x fbh XRGB8888 (the VIG0-scaled mode, boot-args fbw/fbh):
 *           the VIG0 pipe -- the only one of this MDP with a scaler (Linux
 *           mdp5_cfg.c, msm8x53_config) -- fetches the splash memory at the
 *           smaller size and stretches it to the 1080x2280 panel, bilinear,
 *           in place of the RGB pipe the ABL set up. The processor neither
 *           draws the panel's pixels nor scales them.
 *
 * Register layouts: LK (lk2nd) platform/msm_shared/mdp5.c and include/mdp5.h,
 * and the mia1 shim's console.c, hardware-proven on this MDP (1.16).
 */
#include <stdint.h>
#include "console.h"
#include "font.h"

#define MDP_BASE		0x01a00000UL
#define MDP_CTL0_FLUSH		(MDP_BASE + 0x2000 + 0x18)
#define MDP_CTL0_LAYER0		(MDP_BASE + 0x2000 + 0x00)
#define VIG0			(MDP_BASE + 0x05000)
#define RGB0			(MDP_BASE + 0x15000)

#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK		0x34
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC_IMG_SIZE	0x04
#define SSPP_SRC_XY		0x08
#define SSPP_OUT_SIZE		0x0c
#define SSPP_OUT_XY		0x10
#define SSPP_SRC_OP_MODE	0x38
#define SSPP_REQPRIO_WM(n)	(0x50 + 4 * (n))
#define SSPP_SRC_ADDR_SW_STATUS	0x70
#define SSPP_DECIMATION		0xb4
#define SSPP_PIX_EXT_LR(c)	(0x100 + 0x10 * (c))
#define SSPP_PIX_EXT_TB(c)	(0x104 + 0x10 * (c))
#define SSPP_PIX_EXT_REQ(c)	(0x108 + 0x10 * (c))

#define VIG_OP_MODE		0x200		/* [17] CSC: off for RGB */
#define VIG_SCALE_CONFIG	0x204
#define VIG_PHASE_STEP_X	0x210
#define VIG_PHASE_STEP_Y	0x214
#define VIG_CR_PHASE_STEP_X	0x218
#define VIG_CR_PHASE_STEP_Y	0x21c
#define VIG_INIT_PHASE_X	0x220
#define VIG_INIT_PHASE_Y	0x224

#define FORMAT_XRGB8888		0x000236ffu	/* 4 components of 8 bits, 4 bytes, tight */
#define UNPACK_XRGB8888		0x03020001u	/* bytes in memory: blue, green, red, x */
#define OP_MODE_SW_PIX_EXT	(1u << 31)
#define SCALE_XY_BILINEAR	0x00055503u	/* X and Y on; bilinear for every component */

#define FLUSH_VIG0		(1u << 0)
#define FLUSH_LM0		(1u << 6)
#define FLUSH_CTL		(1u << 17)

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

static inline uint32_t mdp_rd(uint32_t off) { return *(volatile uint32_t *)(MDP_BASE + off); }
static inline void mdp_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(MDP_BASE + off) = v; }

volatile uint8_t *con_fb = (volatile uint8_t *)FB_BASE;
static uint32_t con_bg;
static int row;

/* the scaled mode: the MDP fetches FB_BASE as w x h XRGB8888 and scales it to
 * the panel; the console writes 32-bpp words at w x h (the font at 2x2) */
static uint32_t con_w = FB_W, con_h = FB_H, con_stride = FB_STRIDE;
static int con_bpp = 24;

static uint32_t
c24_to_xrgb(uint32_t color)
{
	/* color 0x00BBGGRR -> the XRGB8888 word B | G << 8 | R << 16 */
	return ((color >> 16) & 0xffu) | (color & 0xff00u) | ((color & 0xffu) << 16);
}

void
con_switch_scaled(uint32_t w, uint32_t h)
{
	uint32_t step_x = (uint32_t)(((uint64_t)w << 21) / FB_W);
	uint32_t step_y = (uint32_t)(((uint64_t)h << 21) / FB_H);
	uint32_t layer;
	int i;

	/* evidence first: which pipe does the ABL's splash actually use? The live
	 * pipe's SRC_SIZE reads the panel's 1080x2280 (0x08e80438); the mixer stage
	 * register names it. The mia1 ABL used RGB0; daisy's may differ. */
	con_hex2("MDP RGB0", "src", mdp_rd(RGB0 + SSPP_SRC_SIZE), "layer", mdp_rd(MDP_CTL0_LAYER0));
	con_hex2("MDP VIG0", "src", mdp_rd(VIG0 + SSPP_SRC_SIZE), "op", mdp_rd(VIG0 + VIG_OP_MODE));

	mdp_wr(VIG0 + SSPP_SRC0_ADDR, (uint32_t)FB_BASE);
	mdp_wr(VIG0 + SSPP_SRC_YSTRIDE, w * 4);
	mdp_wr(VIG0 + SSPP_SRC_IMG_SIZE, h << 16 | w);
	mdp_wr(VIG0 + SSPP_SRC_SIZE, h << 16 | w);
	mdp_wr(VIG0 + SSPP_SRC_XY, 0);
	mdp_wr(VIG0 + SSPP_OUT_SIZE, FB_H << 16 | FB_W);
	mdp_wr(VIG0 + SSPP_OUT_XY, 0);
	mdp_wr(VIG0 + SSPP_SRC_FORMAT, FORMAT_XRGB8888);
	mdp_wr(VIG0 + SSPP_SRC_UNPACK, UNPACK_XRGB8888);
	mdp_wr(VIG0 + SSPP_SRC_ADDR_SW_STATUS, 0);
	mdp_wr(VIG0 + SSPP_DECIMATION, 0);
	for (i = 0; i < 3; i++) {
		/* the RGB pipe's FIFO watermarks carry over; bilinear reads one pixel
		 * past the right and bottom edges: repeat the last */
		mdp_wr(VIG0 + SSPP_REQPRIO_WM(i), mdp_rd(RGB0 + SSPP_REQPRIO_WM(i)));
		mdp_wr(VIG0 + SSPP_PIX_EXT_LR(i), 1u << 16);
		mdp_wr(VIG0 + SSPP_PIX_EXT_TB(i), 1u << 16);
		mdp_wr(VIG0 + SSPP_PIX_EXT_REQ(i), (h + 1) << 16 | (w + 1));
	}
	mdp_wr(VIG0 + VIG_OP_MODE, 0);
	mdp_wr(VIG0 + VIG_PHASE_STEP_X, step_x);
	mdp_wr(VIG0 + VIG_PHASE_STEP_Y, step_y);
	mdp_wr(VIG0 + VIG_CR_PHASE_STEP_X, step_x);
	mdp_wr(VIG0 + VIG_CR_PHASE_STEP_Y, step_y);
	mdp_wr(VIG0 + VIG_INIT_PHASE_X, 0);
	mdp_wr(VIG0 + VIG_INIT_PHASE_Y, 0);
	mdp_wr(VIG0 + VIG_SCALE_CONFIG, SCALE_XY_BILINEAR);
	mdp_wr(VIG0 + SSPP_SRC_OP_MODE, OP_MODE_SW_PIX_EXT);
	/* the mixer's stage: the ABL's splash pipe keeps its stage number, VIG0 takes
	 * it (RGB0 [11:9] -> VIG0 [2:0] as the mia1 ABL had RGB0; daisy's ABL may
	 * already have VIG0 there, in which case the register is left alone) */
	layer = mdp_rd(MDP_CTL0_LAYER0);
	uint32_t rgb_stage = (layer >> 9) & 7u, vig_stage = layer & 7u;
	if (rgb_stage != 0) {
		mdp_wr(MDP_CTL0_LAYER0, (layer & ~(7u << 9 | 7u)) | rgb_stage);
	} else {
		mdp_wr(MDP_CTL0_LAYER0, (layer & ~(7u << 9 | 7u)) | vig_stage);
	}
	mdp_wr(MDP_CTL0_FLUSH, FLUSH_VIG0 | FLUSH_LM0 | FLUSH_CTL);
	con_hex2("MDP VIG0", "stage", mdp_rd(MDP_CTL0_LAYER0) & 7u, "flush", 1);

	con_w = w;
	con_h = h;
	con_stride = w * 4;
	con_bpp = 32;
}

uint32_t con_scaled_w(void) { return con_w; }
uint32_t con_scaled_h(void) { return con_h; }
int con_is_scaled(void) { return con_bpp == 32; }

void
con_clear(uint32_t bg)
{
	if (con_bpp == 32) {
		volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
		uint32_t v = c24_to_xrgb(bg);
		for (uint32_t y = 0; y < con_h; y++) {
			volatile uint32_t *line = fb + (uint64_t)y * (con_stride / 4);
			for (uint32_t x = 0; x < con_w; x++)
				line[x] = v;
		}
	} else {
		volatile uint8_t *line;
		uint8_t b = (bg >> 0) & 0xff;
		uint8_t g = (bg >> 8) & 0xff;
		uint8_t r = (bg >> 16) & 0xff;
		for (int y = 0; y < FB_H; y++) {
			line = con_fb + (uint64_t)y * FB_STRIDE;
			for (int x = 0; x < FB_W; x++) {
				line[x * 3 + 0] = b;
				line[x * 3 + 1] = g;
				line[x * 3 + 2] = r;
			}
		}
	}
	con_bg = bg;
	row = 0;
}

static void
put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	uint32_t fgv = c24_to_xrgb(fg);
	uint32_t bgv = c24_to_xrgb(con_bg);
	int x0, y0;

	if (con_bpp == 32) {
		volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
		x0 = MARGIN + col * 16;
		y0 = MARGIN + row * 32;
		if (x0 + 16 > (int)con_w || y0 + 32 > (int)con_h)
			return;
		for (int gy = 0; gy < 16; gy++) {
			for (int gx = 0; gx < 8; gx++) {
				int on = (g[gy] & (0x80 >> gx)) != 0;
				uint32_t c = on ? fgv : bgv;
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++)
						fb[(uint64_t)(y0 + gy * 2 + sy) * (con_stride / 4) + x0 + gx * 2 + sx] = c;
			}
		}
		return;
	}

	x0 = MARGIN + col * CH_W;
	y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;

	uint8_t fg_b = (fg >> 0) & 0xff;
	uint8_t fg_g = (fg >> 8) & 0xff;
	uint8_t fg_r = (fg >> 16) & 0xff;

	uint8_t bg_b = (con_bg >> 0) & 0xff;
	uint8_t bg_g = (con_bg >> 8) & 0xff;
	uint8_t bg_r = (con_bg >> 16) & 0xff;

	for (int gy = 0; gy < 16; gy++) {
		for (int gx = 0; gx < 8; gx++) {
			int on = (g[gy] & (0x80 >> gx)) != 0;
			uint8_t b = on ? fg_b : bg_b;
			uint8_t gr = on ? fg_g : bg_g;
			uint8_t r = on ? fg_r : bg_r;

			for (int sy = 0; sy < SCALE; sy++) {
				volatile uint8_t *p = con_fb + (uint64_t)(y0 + gy * SCALE + sy) * FB_STRIDE
						       + (uint64_t)(x0 + gx * SCALE) * 3;
				for (int sx = 0; sx < SCALE; sx++) {
					p[sx * 3 + 0] = b;
					p[sx * 3 + 1] = gr;
					p[sx * 3 + 2] = r;
				}
			}
		}
	}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_str_color(const char *label, const char *s, uint32_t color)
{
	put_str(0, label, color);
	put_str(LABEL_COLS, s, color);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
