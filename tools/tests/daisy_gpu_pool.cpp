static void checkLeaseDuringTeardown(uint64_t pa, uint64_t size) {
    // The persistent prepared VM entry must be torn down while its span is
    // still reserved. A concurrent allocator must not reuse it sooner.
    for (unsigned i = 0; i < gPoolCount; ++i) {
        if (pa < gPool[i].address || pa - gPool[i].address >= (uint64_t)gPool[i].pages * PAGE_SIZE)
            continue;
        unsigned first = (pa - gPool[i].address) / PAGE_SIZE;
        for (unsigned p = first; p < first + size / PAGE_SIZE; ++p)
            assert(gpuPoolPageUsed(i, p));
        return;
    }
    assert(false);
}

static IOMemoryDescriptor *allocate(uint64_t size, uint8_t **bytes = nullptr) {
    uint8_t *kernel; uint64_t address;
    auto md = gpuGemAllocate(size, &kernel, &address);
    if (md) {
        IOByteCount length;
        assert(md->getPhysicalSegment(0, &length, kIOMemoryMapperNone) == address && length == size);
        assert(address % PAGE_SIZE == 0 && address + size <= GPU_ADDRESS_LIMIT);
        if (bytes) *bytes = kernel;
    }
    return md;
}

static void cleanup() {
    assert(gPoolUsed == 0);
    assert(gPoolTakes == gPoolReturns);
    assert(preparations == completions);
    for (unsigned i = 0; i < gPoolCount; ++i) {
        for (auto bits : gPool[i].used) assert(bits == 0);
        gPool[i].memory->release(); gPool[i] = {};
    }
    delete gPoolLock; gPoolLock = nullptr;
    gPoolCount = gPoolPeak = gPoolMisses = gPoolFallbacks = 0;
    gPoolReserved = gPoolTakes = gPoolReturns = 0;
    assert(allocations.empty());
    nextPA = 0xa4000000;
}

static IOMemoryDescriptor *surface(uint32_t size) {
    uintptr_t address = 0xdeadbeef;
    auto md = vince_surface_allocate(size, &address);
    if (md) assert(address != 0xdeadbeef && address != 0);
    else assert(address == 0xdeadbeef);
    return md;
}

static void exercise() {
    poolMB = 0;
    failPhysicalAt = 0;
    // Without the boot argument, use the original allocator.
    gpuPoolInit(); assert(gPoolCount == 0 && !gPoolLock);
    auto ordinary = allocate(PAGE_SIZE); assert(ordinary); ordinary->release();
    ordinary = surface(PAGE_SIZE); assert(ordinary); ordinary->release();
    auto calls = physicalCalls;
    gpuSurfaceDMA = 0; assert(!surface(PAGE_SIZE)); gpuSurfaceDMA = 1;
    gGPUSurfaces = false; assert(!surface(PAGE_SIZE)); gGPUSurfaces = true;
    assert(!surface(0) && !surface(17) && !allocate(0) && !allocate(17));
    assert(physicalCalls == calls);
    poolMB = 72; gpuPoolInit(); assert(gPoolCount == 2);
    calls = physicalCalls;
    constexpr uint64_t target = 12u << 20;
    auto a = allocate(target), b = allocate(target), c = allocate(target), d = allocate(target);
    auto e = surface(target), f = surface(target);
    assert(a && b && c && d && e && f);
    assert(allocate(target) == nullptr && surface(target) == nullptr);
    assert(physicalCalls == calls); // exhaustion must not enter global VM scanning
    b->release(); d->release();

    // Reproduce the repeated 12 MiB target requests without losing physical runs.
    for (unsigned i = 0; i < 111; ++i) {
        uint8_t *bytes;
        auto targetMemory = allocate(target, &bytes); assert(targetMemory);
        for (size_t p = 0; p < target; ++p) assert(bytes[p] == 0);
        memset(bytes, 0xa5, target);
        targetMemory->release();
    }
    a->release(); c->release(); e->release(); f->release();
    assert(gPoolUsed == 0 && physicalCalls == calls);

    // GEM_CLOSE releases the handle, but a live user mapping still owns the range.
    a = allocate(GPU_POOL_CHUNK); b = allocate(GPU_POOL_CHUNK); assert(a && b);
    auto map = a->map(); uint64_t oldPA = a->pa;
    a->release(); assert(gPoolUsed == 2 * GPU_POOL_PAGES && !allocate(2u << 20));
    // Only requests <=1 MiB may use the ordinary allocator. Surface requests
    // must not retry the same failed call or perform a large contiguous scan.
    assert(!surface((1u << 20) + PAGE_SIZE) && physicalCalls == calls);
    ordinary = surface(1u << 20); assert(ordinary && physicalCalls == calls + 1);
    ordinary->release();
    failPhysicalAt = physicalCalls + 1;
    assert(!surface(PAGE_SIZE) && physicalCalls == calls + 2);
    failPhysicalAt = 0; calls = physicalCalls;
    map->release(); assert(gPoolUsed == GPU_POOL_PAGES);
    a = allocate(GPU_POOL_CHUNK); assert(a && a->pa == oldPA);
    a->release(); b->release();

    // Descriptor failure unwinds its lease; adjacent frees coalesce for a whole chunk.
    failDescriptor = true; assert(!allocate(target) && gPoolUsed == 0); failDescriptor = false;
    failSpan = true; assert(!allocate(target) && gPoolUsed == 0); failSpan = false;
    assert(preparations == completions);
    std::vector<IOMemoryDescriptor *> pieces;
    for (unsigned i = 0; i < 144; ++i) { a = allocate(512u << 10); assert(a); pieces.push_back(a); }
    // Check disjoint physical leases, then free in alternating order to
    // exercise bitmap coalescing across many independent descriptors.
    for (unsigned i = 0; i < pieces.size(); ++i)
        for (unsigned j = i + 1; j < pieces.size(); ++j)
            assert(pieces[i]->pa + pieces[i]->size <= pieces[j]->pa ||
                   pieces[j]->pa + pieces[j]->size <= pieces[i]->pa);
    for (unsigned parity = 0; parity < 2; ++parity)
        for (unsigned i = parity; i < pieces.size(); i += 2) pieces[i]->release();
    assert(gPoolUsed == 0);
    a = allocate(GPU_POOL_CHUNK); b = allocate(GPU_POOL_CHUNK); assert(a && b);
    a->release(); b->release();
    assert(!allocate(0) && !allocate(17) && !allocate(GPU_POOL_CHUNK + PAGE_SIZE));
    assert(physicalCalls == calls);
    cleanup();

    // A partial early reserve is still usable and does not retry failed chunks later.
    poolMB = 96; failPhysicalAt = physicalCalls + 2; gpuPoolInit(); assert(gPoolCount == 1);
    calls = physicalCalls;
    gpuPoolInit(); assert(physicalCalls == calls);
    assert(gPoolReserved == GPU_POOL_PAGES);
    a = allocate(target); assert(a && physicalCalls == calls); a->release(); cleanup();

    // An explicitly requested reserve that fails completely must also avoid
    // large per-frame VM retries. Small fallback remains available.
    failPhysicalAt = physicalCalls + 1; gpuPoolInit(); assert(!gPoolCount && gPoolLock);
    calls = physicalCalls;
    assert(!allocate(target) && !surface(target) && physicalCalls == calls);
    a = surface(PAGE_SIZE); assert(a && physicalCalls == calls + 1); a->release(); cleanup();

    // Exact small/partial budgets, including a final chunk that is not 36 MiB.
    failPhysicalAt = 0;
    for (auto mb : {1, 35, 37, 64}) {
        poolMB = mb; gpuPoolInit();
        assert((uint64_t)gPoolReserved * PAGE_SIZE == (uint64_t)mb << 20);
        std::vector<IOMemoryDescriptor *> chunks;
        for (unsigned i = 0; i < gPoolCount; ++i) {
            a = allocate((uint64_t)gPool[i].pages * PAGE_SIZE); assert(a); chunks.push_back(a);
        }
        calls = physicalCalls;
        assert(!surface(2u << 20) && physicalCalls == calls);
        for (auto md : chunks) md->release();
        cleanup();
    }

    // The actual failed surface size (rounded to runtime pages) and Mesa's
    // 12 MiB replacement each fit 42 times within 512 MiB, versus 32 before.
    // A request above the cap must still reserve only 512 MiB.
    for (auto request : {11227136u, 12u << 20}) {
        poolMB = 1024; gpuPoolInit();
        assert(gPoolCount == 15 && (uint64_t)gPoolReserved * PAGE_SIZE == (512u << 20));
        assert(gPool[14].pages * PAGE_SIZE == (8u << 20));
        calls = physicalCalls;
        uint32_t size = (request + PAGE_MASK) & ~PAGE_MASK;
        pieces.clear();
        for (unsigned i = 0; i < 42; ++i) {
            a = i % 2 ? allocate(size) : surface(size); assert(a); pieces.push_back(a);
        }
        assert(!surface(size) && !allocate(size) && physicalCalls == calls);
        // Map lifetimes keep ranges leased, even at full-size surface pressure.
        auto held = pieces[0]->map();
        pieces[0]->release(); assert(!surface(size));
        held->release();
        pieces[0] = surface(size); assert(pieces[0]);
        for (auto md : pieces) md->release();
        assert(physicalCalls == calls); cleanup();
    }
    printf("PASS (%u-byte runtime pages): 42 large targets in 512 MiB, exact budgets, bounded GEM/surface fallback, failed reserve, 111 zero-filled reuses, disjoint leases and mapping lifetime\n", PAGE_SIZE);
}

int main() {
    for (auto size : {4096u, 16384u}) {
        vm_page_size = size;
        exercise();
    }
}
