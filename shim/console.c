/*
 * Minimal text console on the ABL splash framebuffer (24-bit RGB888).
 * Stride = 1080 * 3 = 3240 bytes.
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

volatile uint8_t *con_fb = (volatile uint8_t *)FB_BASE;
static uint32_t con_bg;
static int row;

/* the scaled mode (boot-args fbw/fbh): the MDP5's VIG0 pipe -- the only one of
 * this MDP with a scaler (Linux mdp5_cfg.c, msm8x53_config) -- fetches the same
 * splash memory at the smaller size and stretches it to the panel, bilinear.
 * The console and the framebuffer geometry switch to the fetch size; the panel
 * timing, mixer stage and interface stay as the ABL set them. Register layouts:
 * LK (lk2nd) platform/msm_shared/mdp5.c, and the mia1 shim's console.c,
 * hardware-proven on this MDP (1.16). */
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

#define OP_MODE_SW_PIX_EXT	(1u << 31)
#define SCALE_XY_BILINEAR	0x00055503u	/* X and Y on; bilinear for every component */

#define FLUSH_VIG0		(1u << 0)
#define FLUSH_LM0		(1u << 6)
#define FLUSH_CTL		(1u << 17)

static uint32_t con_w = FB_W, con_h = FB_H, con_stride = FB_STRIDE;

static inline uint32_t mdp_rd(uint32_t off) { return *(volatile uint32_t *)(MDP_BASE + off); }
static inline void mdp_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(MDP_BASE + off) = v; }

/*
 * The live splash pipe (whichever of RGB0/VIG0 reads the panel's 1080x2280) is
 * reprogrammed IN PLACE: source 750 x 1624 at the 24-bpp fetch format the ABL
 * already set, stride 2250, and the VIG0 QSEED scaler stretching it to the
 * unchanged OUT size. The mixer stage and the interface stay as they are.
 */
int
con_switch_scaled(uint32_t w, uint32_t h)
{
	uint32_t rgb_src = mdp_rd(RGB0 + SSPP_SRC_SIZE);
	uint32_t vig_src = mdp_rd(VIG0 + SSPP_SRC_SIZE);
	uint32_t panel_src = (uint32_t)(FB_H << 16 | FB_W);
	uint32_t base, pipe_flush;

	con_hex2("MDP RGB0", "src", rgb_src, "vig", vig_src);
	con_hex("MDP LAYER0", mdp_rd(MDP_CTL0_LAYER0));

	/* 0211d probe-only: the 0211c boot force-rebooted, so this build only
	 * logs the MDP state and takes the known-good 1080x2280 path -- the
	 * values on screen name the ABL's live pipe and fetch layout. */
	return 0;

	if (rgb_src == panel_src) {
		base = RGB0;
		pipe_flush = FLUSH_LM0;			/* the RGB pipe has no scaler */
		con_str("MDP", "the splash is on RGB0: no scaler on this pipe");
		return 0;
	}
	if (vig_src != panel_src) {
		con_str("MDP", "neither pipe reads the panel size; refusing");
		return 0;
	}
	base = VIG0;
	pipe_flush = FLUSH_VIG0;

	mdp_wr(base + SSPP_SRC0_ADDR, (uint32_t)FB_BASE);
	mdp_wr(base + SSPP_SRC_YSTRIDE, w * 3);
	mdp_wr(base + SSPP_SRC_IMG_SIZE, h << 16 | w);
	mdp_wr(base + SSPP_SRC_SIZE, h << 16 | w);
	mdp_wr(base + SSPP_SRC_XY, 0);
	mdp_wr(base + SSPP_OUT_SIZE, FB_H << 16 | FB_W);
	mdp_wr(base + SSPP_OUT_XY, 0);
	mdp_wr(base + SSPP_SRC_ADDR_SW_STATUS, 0);
	mdp_wr(base + SSPP_DECIMATION, 0);
	for (int i = 0; i < 3; i++) {
		/* bilinear reads one pixel past the right and bottom edges: repeat the last */
		mdp_wr(base + SSPP_PIX_EXT_LR(i), 1u << 16);
		mdp_wr(base + SSPP_PIX_EXT_TB(i), 1u << 16);
		mdp_wr(base + SSPP_PIX_EXT_REQ(i), (h + 1) << 16 | (w + 1));
	}
	mdp_wr(base + VIG_OP_MODE, 0);
	mdp_wr(base + VIG_PHASE_STEP_X, (uint32_t)(((uint64_t)w << 21) / FB_W));
	mdp_wr(base + VIG_PHASE_STEP_Y, (uint32_t)(((uint64_t)h << 21) / FB_H));
	mdp_wr(base + VIG_CR_PHASE_STEP_X, (uint32_t)(((uint64_t)w << 21) / FB_W));
	mdp_wr(base + VIG_CR_PHASE_STEP_Y, (uint32_t)(((uint64_t)h << 21) / FB_H));
	mdp_wr(base + VIG_INIT_PHASE_X, 0);
	mdp_wr(base + VIG_INIT_PHASE_Y, 0);
	mdp_wr(base + VIG_SCALE_CONFIG, SCALE_XY_BILINEAR);
	mdp_wr(base + SSPP_SRC_OP_MODE, OP_MODE_SW_PIX_EXT);
	mdp_wr(MDP_CTL0_FLUSH, pipe_flush | FLUSH_CTL);

	con_w = w;
	con_h = h;
	con_stride = w * 3;
	return 1;
}

void con_clear(uint32_t bg)
{
	uint8_t b = (bg >> 0) & 0xff;
	uint8_t g = (bg >> 8) & 0xff;
	uint8_t r = (bg >> 16) & 0xff;

	for (int y = 0; y < (int)con_h; y++) {
		volatile uint8_t *line = con_fb + (uint64_t)y * con_stride;
		for (int x = 0; x < (int)con_w; x++) {
			line[x * 3 + 0] = b;
			line[x * 3 + 1] = g;
			line[x * 3 + 2] = r;
		}
	}
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > (int)con_w || y0 + CH_H > (int)con_h)
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
				volatile uint8_t *p = con_fb + (uint64_t)(y0 + gy * SCALE + sy) * con_stride
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
