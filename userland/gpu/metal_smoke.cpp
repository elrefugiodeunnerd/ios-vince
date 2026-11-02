// The a5xx Metal smoke test, opt-in by boot-arg metalsmoke=1 and injected into backboardd with the rest of the
// GPU stack: a device on the kernel's GPU service, one fill_id dispatch compiled by the phone's own AIR front
// end, and the buffer read back. stderr here is the console, so the result reaches the persistent log.
//
// fill_id (tools/kernels/fill_id.ll, ours, MIT): out[gid] = gid * 0x9E3779B1u + c. The smoke binds out as
// [[buffer(0)]] and a two-word constants buffer with c = 0x00C0FFEE as [[buffer(1)]], runs a grid of 4x1x1
// work groups of one thread each, and checks the four words.
//
// The kernel arrives as LLVM 14/15 bitcode (kFillIdBc below, llvm-as-15 of tools/kernels/fill_id.ll); the
// phone's hakios_metal_kernel_compile_bitcode compiles it for THIS chip (the AIR front end in the iOS Mesa
// build, ir3 for the Adreno the device names), so the machine code is the A506's own.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <sys/sysctl.h>

#include "hakios-metal/hakios_metal.h"

#include "metal_smoke_fill_id_bc.h"
#include "metal_smoke_tri_bc.h"

static const uint32_t kConstantValue = 0x00C0FFEEu;
static const uint32_t kMagic = 0x9E3779B1u;

// The render half, opt-in by rendersmoke=1: one tri_flat draw over the whole
// target (six vertices, the lower-left half and its complement), then every
// pixel read back against the flat colour 0x20,0x40,0x80,0xff (RGBA8Unorm).
// The draw exercises render5's a5xx sequence: the restore, the pass, the
// program preload, the raster state, CP_DRAW_INDX_OFFSET and both flushes.
static void
metal_render_smoke(hakios_metal_device *device)
{
	fprintf(stderr, "RENDER SMOKE: starting (tri_flat over a 64x64 RGBA8 target)\n");

	hakios_metal_texture *target = hakios_metal_texture_new(device, 2, 70, 64, 64,
	    HAKIOS_METAL_TEXTURE_USAGE_RENDER_TARGET);
	if (target == NULL) {
		fprintf(stderr, "RENDER SMOKE: FAIL: no target texture: %s\n", hakios_metal_error());
		return;
	}

	hakios_metal_program *program = hakios_metal_program_new_bitcode(kTriFlatVsBc, kTriFlatVsBcSize,
	    kTriFlatFsBc, kTriFlatFsBcSize, 70);
	if (program == NULL) {
		fprintf(stderr, "RENDER SMOKE: FAIL: the program did not compile: %s\n", hakios_metal_error());
		return;
	}
	fprintf(stderr, "RENDER SMOKE: program compiled from the embedded AIR bitcodes\n");

	hakios_metal_queue *queue = hakios_metal_queue_new(device);
	hakios_metal_cb *cb = hakios_metal_cb_new(queue);

	struct hakios_metal_render_pass pass;
	memset(&pass, 0, sizeof(pass));
	pass.color0 = target;
	pass.load_action = 1;              // Load: the draw covers the whole target
	pass.store_action = 1;             // Store
	struct hakios_metal_viewport viewport = {0, 0, 64, 64, 0, 1};
	int r = hakios_metal_cb_draw(cb, program, &pass, &viewport, nullptr,
	    3 /* triangle */, 0, 6 /* both halves */, 1, nullptr, nullptr, 0, nullptr, nullptr, 0);
	if (r != 0) {
		fprintf(stderr, "RENDER SMOKE: FAIL: the draw was refused: %s\n", hakios_metal_error());
		return;
	}
	fprintf(stderr, "RENDER SMOKE: draw encoded, submitting\n");
	hakios_metal_cb_commit(cb, nullptr, nullptr);
	hakios_metal_cb_wait(cb);
	if (hakios_metal_cb_status(cb) != HAKIOS_METAL_COMPLETED) {
		fprintf(stderr, "RENDER SMOKE: FAIL: submit/fence: %s\n", hakios_metal_cb_error(cb));
		return;
	}

	const unsigned char *pixels = (const unsigned char *)hakios_metal_texture_contents(target);
	if (pixels == NULL) {
		fprintf(stderr, "RENDER SMOKE: FAIL: the target is not mapped\n");
		return;
	}
	// RGBA8Unorm rows are 64*4 bytes, the layout texture.hpp lays a linear single-level 2D out as
	int bad = 0;
	for (unsigned y = 0; y < 64 && bad < 4; y++) {
		for (unsigned x = 0; x < 64; x++) {
			const unsigned char *px = pixels + (y * 64 + x) * 4;
			if (px[0] != 0x20 || px[1] != 0x40 || px[2] != 0x80 || px[3] != 0xff) {
				fprintf(stderr, "RENDER SMOKE: pixel (%u,%u) is %02x %02x %02x %02x, want 20 40 80 ff\n",
				    x, y, px[0], px[1], px[2], px[3]);
				bad++;
				break;
			}
		}
	}
	if (bad == 0) {
		fprintf(stderr, "RENDER SMOKE: PASS: 4096 pixels of the a5xx draw read back flat\n");
	} else {
		fprintf(stderr, "RENDER SMOKE: FAIL: %d rows wrong\n", bad);
	}
}

// Called by the injector AFTER dlopen returns. A constructor here would run
// before the statically linked driver's maps (command.cpp and texture.cpp).
extern "C" void
daisy_metal_smoke(void)
{
	char args[1024] = "";
	size_t size = sizeof(args) - 1;
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) != 0 || strstr(args, " metalsmoke=1") == NULL && strstr(args, " rendersmoke=1") == NULL) {
		return;
	}

	fprintf(stderr, "METAL SMOKE: starting (fill_id on the device's own compiler)\n");

	std::unique_ptr<hakios_metal_device, decltype(&hakios_metal_close)> deviceOwner(hakios_metal_open(), hakios_metal_close);
	hakios_metal_device *device = deviceOwner.get();
	if (device == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: no device: %s\n", hakios_metal_error());
		return;
	}
	fprintf(stderr, "METAL SMOKE: device open, gpu id 0x%llx\n",
	    (unsigned long long)hakios_metal_gpu_id(device));
	if (hakios_metal_gpu_id(device) != 0x05000600) {
		fprintf(stderr, "METAL SMOKE: FAIL: expected the A506\n");
		return;
	}
	if (strstr(args, " rendersmoke=1") != NULL && strstr(args, " metalsmoke=1") == NULL) {
		metal_render_smoke(device);
		return;
	}

	hakios_metal_buffer *out = hakios_metal_buffer_new(device, 64);
	std::unique_ptr<hakios_metal_buffer, decltype(&hakios_metal_buffer_free)> outOwner(out, hakios_metal_buffer_free);
	hakios_metal_buffer *cbuf = hakios_metal_buffer_new(device, 16);
	std::unique_ptr<hakios_metal_buffer, decltype(&hakios_metal_buffer_free)> constantsOwner(cbuf, hakios_metal_buffer_free);
	if (out == NULL || cbuf == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: no buffers: %s\n", hakios_metal_error());
		return;
	}
	uint32_t *c = (uint32_t *)hakios_metal_buffer_contents(cbuf);
	uint32_t *output = (uint32_t *)hakios_metal_buffer_contents(out);
	if (c == NULL || output == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: buffers are not mapped\n");
		return;
	}
	memset(c, 0, 16);
	memset(output, 0xa5, 64);
	c[0] = kConstantValue;
	fprintf(stderr, "METAL SMOKE: GEM allocated and mapped, out=0x%llx constants=0x%llx\n",
	    (unsigned long long)hakios_metal_buffer_gpu_address(out),
	    (unsigned long long)hakios_metal_buffer_gpu_address(cbuf));
	fprintf(stderr, "METAL SMOKE: compiling AIR for chip 0x05000600\n");

	hakios_metal_kernel *k = hakios_metal_kernel_compile_bitcode_on_device(device, kFillIdBc, kFillIdBcSize);
	std::unique_ptr<hakios_metal_kernel, decltype(&hakios_metal_kernel_free)> kernelOwner(k, hakios_metal_kernel_free);
	if (k == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: the kernel did not compile: %s\n", hakios_metal_error());
		return;
	}
	unsigned threads[3];
	hakios_metal_kernel_threadgroup_size(k, threads);
	// fill_id has a variable workgroup size. This smoke chooses one invocation
	// per group so the four groups write exactly out[0..3].
	for (unsigned i = 0; i < 3; i++) {
		if (threads[i] == 0) threads[i] = 1;
	}
	if (threads[0] != 1 || threads[1] != 1 || threads[2] != 1) {
		fprintf(stderr, "METAL SMOKE: FAIL: fill_id requires a 1x1x1 smoke workgroup\n");
		return;
	}
	fprintf(stderr, "METAL SMOKE: kernel compiled: %s, threadgroup %ux%ux%u\n",
	    hakios_metal_kernel_name(k), threads[0], threads[1], threads[2]);

	hakios_metal_queue *queue = hakios_metal_queue_new(device);
	std::unique_ptr<hakios_metal_queue, decltype(&hakios_metal_queue_free)> queueOwner(queue, hakios_metal_queue_free);
	if (queue == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: queue: %s\n", hakios_metal_error());
		return;
	}
	hakios_metal_cb *cb = hakios_metal_cb_new(queue);
	std::unique_ptr<hakios_metal_cb, decltype(&hakios_metal_cb_free)> cbOwner(cb, hakios_metal_cb_free);
	if (cb == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: command buffer: %s\n", hakios_metal_error());
		return;
	}
	hakios_metal_buffer *buffers[2] = {out, cbuf};
	uint64_t offsets[2] = {0, 0};
	const unsigned groups[3] = {4, 1, 1};
	int r = hakios_metal_cb_dispatch2(cb, k, groups, threads,
	    (hakios_metal_buffer * const*)buffers, offsets, 2, nullptr, 0, nullptr, 0);
	if (r != 0) {
		fprintf(stderr, "METAL SMOKE: FAIL: the dispatch was refused: %s\n", hakios_metal_error());
		return;
	}
	fprintf(stderr, "METAL SMOKE: dispatch encoded, submitting 4 workgroups\n");
	hakios_metal_cb_commit(cb, nullptr, nullptr);
	hakios_metal_cb_wait(cb);
	if (hakios_metal_cb_status(cb) != HAKIOS_METAL_COMPLETED) {
		fprintf(stderr, "METAL SMOKE: FAIL: submit/fence: %s\n", hakios_metal_cb_error(cb));
		return;
	}
	fprintf(stderr, "METAL SMOKE: fence completed, reading back\n");

	const uint32_t *outWords = (const uint32_t *)hakios_metal_buffer_contents(out);
	if (outWords == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: the out buffer is not mapped\n");
		return;
	}
	int bad = 0;
	for (uint32_t gid = 0; gid < 4; gid++) {
		uint32_t want = gid * kMagic + kConstantValue;
		if (outWords[gid] != want) {
			fprintf(stderr, "METAL SMOKE: out[%u] is 0x%08x, want 0x%08x\n", gid, outWords[gid], want);
			bad++;
		} else {
			fprintf(stderr, "METAL SMOKE: out[%u] = 0x%08x correct\n", gid, outWords[gid]);
		}
	}
	if (bad == 0) {
		fprintf(stderr, "METAL SMOKE: PASS: the A506 ran the a5xx dispatch and the buffer reads back\n");
	} else {
		fprintf(stderr, "METAL SMOKE: FAIL: %d of 4 words wrong\n", bad);
	}
	metal_render_smoke(device);

}
