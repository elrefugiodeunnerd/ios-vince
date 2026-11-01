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
#include <sys/sysctl.h>

#include "hakios-metal/hakios_metal.h"

#include "metal_smoke_fill_id_bc.h"

static const uint32_t kConstantValue = 0x00C0FFEEu;
static const uint32_t kMagic = 0x9E3779B1u;

__attribute__((constructor))
static void
metal_smoke(void)
{
	char args[1024] = "";
	size_t size = sizeof(args) - 1;
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) != 0 || strstr(args, " metalsmoke=1") == NULL) {
		return;
	}

	fprintf(stderr, "METAL SMOKE: starting (fill_id on the device's own compiler)\n");

	hakios_metal_device *device = hakios_metal_open();
	if (device == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: no device: %s\n", hakios_metal_error());
		return;
	}
	fprintf(stderr, "METAL SMOKE: device open, gpu id 0x%llx\n",
	    (unsigned long long)hakios_metal_gpu_id(device));

	hakios_metal_buffer *out = hakios_metal_buffer_new(device, 64);
	hakios_metal_buffer *cbuf = hakios_metal_buffer_new(device, 16);
	if (out == NULL || cbuf == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: no buffers: %s\n", hakios_metal_error());
		return;
	}
	uint32_t *c = (uint32_t *)hakios_metal_buffer_contents(cbuf);
	if (c == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: the constants buffer is not mapped\n");
		return;
	}
	c[0] = kConstantValue;

	hakios_metal_kernel *k = hakios_metal_kernel_compile_bitcode(kFillIdBc, kFillIdBcSize);
	if (k == NULL) {
		fprintf(stderr, "METAL SMOKE: FAIL: the kernel did not compile: %s\n", hakios_metal_error());
		return;
	}
	unsigned threads[3];
	hakios_metal_kernel_threadgroup_size(k, threads);
	fprintf(stderr, "METAL SMOKE: kernel compiled: %s, threadgroup %ux%ux%u\n",
	    hakios_metal_kernel_name(k), threads[0], threads[1], threads[2]);

	hakios_metal_queue *queue = hakios_metal_queue_new(device);
	hakios_metal_cb *cb = hakios_metal_cb_new(queue);
	hakios_metal_buffer *buffers[2] = {out, cbuf};
	uint64_t offsets[2] = {0, 0};
	int r = hakios_metal_cb_dispatch2(cb, k, (const unsigned[]){4, 1, 1}, (const unsigned[]){threads[0], threads[1], threads[2]},
	    (hakios_metal_buffer * const*)buffers, offsets, 2, nullptr, 0, nullptr, 0);
	if (r != 0) {
		fprintf(stderr, "METAL SMOKE: FAIL: the dispatch was refused: %s\n", hakios_metal_error());
		return;
	}
	hakios_metal_cb_commit(cb, nullptr, nullptr);
	hakios_metal_cb_wait(cb);

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
}
