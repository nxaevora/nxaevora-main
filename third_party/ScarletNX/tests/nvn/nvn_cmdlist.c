/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The host's NVN command lists (gpu/nvn/nvn_cmdlist.c): the op list, the handles of published lists, the references that
   keep a list alive, and the release of the lists that CALL ops name. */
#include "nx/gpu/nvn/nvn_cmdlist.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_append_next(void) {
    NvnOpList l = {0};
    NvnOpBarrier barrier = {0x1234, 0};
    NvnOpViewport viewport = {3, 1.5f, 2.5f, 640.0f, 360.0f};
    uint8_t odd[5] = {1, 2, 3, 4, 5};
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &barrier, sizeof barrier));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &viewport, sizeof viewport));
    CHECK(nvn_oplist_append(&l, NVN_OP_CALL, odd, sizeof odd)); /* padded to 8 */
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, NULL, 0));      /* no payload */
    CHECK(l.ops == 4);

    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BARRIER && h.bytes == sizeof barrier);
    CHECK(((const NvnOpBarrier*)p)->bits == 0x1234);
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_SET_VIEWPORT && h.bytes == 24);
    CHECK(((const NvnOpViewport*)p)->index == 3 && ((const NvnOpViewport*)p)->height == 360.0f);
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_CALL && h.bytes == 8);
    CHECK(!memcmp(p, odd, 5) && ((const uint8_t*)p)[5] == 0 && ((const uint8_t*)p)[7] == 0);
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BARRIER && h.bytes == 0);
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p) && pos == l.used);
    nvn_oplist_free(&l);
    CHECK(!l.data && !l.used && !l.ops);
}

static void test_append_refused(void) {
    NvnOpList l = {0};
    uint8_t big[NVN_OP_MAX_PAYLOAD + 8] = {0};
    CHECK(!nvn_oplist_append(&l, NVN_OP_NONE, big, 8));
    CHECK(!nvn_oplist_append(&l, NVN_OP_COUNT, big, 8));
    CHECK(!nvn_oplist_append(&l, NVN_OP_BARRIER, big, NVN_OP_MAX_PAYLOAD + 1));
    CHECK(!nvn_oplist_append(&l, NVN_OP_BARRIER, NULL, 8));
    CHECK(l.ops == 0 && l.used == 0);
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, big, NVN_OP_MAX_PAYLOAD));
    nvn_oplist_free(&l);
}

static void test_bind_texture(void) {
    NvnOpList l = {0};
    NvnOpBindTexture bind = {2, 7, 0x100000004ull, 0};
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_TEXTURE, &bind, sizeof bind));
    CHECK(l.ops == 1);
    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_TEXTURE && h.bytes == sizeof bind);
    CHECK(((const NvnOpBindTexture*)p)->stage == 2 && ((const NvnOpBindTexture*)p)->slot == 7);
    CHECK(((const NvnOpBindTexture*)p)->handle == 0x100000004ull);
    nvn_oplist_free(&l);
}

static void test_bind_buffer(void) {
    NvnOpList l = {0};
    NvnOpBindBuffer bind = {NVN_BIND_STORAGE, 1, 9, 0, 0x400000000ull, 0x1234};
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_BUFFER, &bind, sizeof bind));
    CHECK(l.ops == 1);
    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_BUFFER && h.bytes == sizeof bind);
    const NvnOpBindBuffer* got = p;
    CHECK(got->kind == NVN_BIND_STORAGE && got->stage == 1 && got->slot == 9);
    CHECK(got->addr == 0x400000000ull && got->size == 0x1234);
    nvn_oplist_free(&l);
}

static void test_bind_state(void) {
    NvnOpList l = {0};
    NvnOpBindState bind = {NVN_STATE_MULTISAMPLE, 5, {5, 0x88888888u, 0x88888888u, 0x88888888u, 0x88888888u}};
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_STATE, &bind, sizeof bind));
    CHECK(l.ops == 1);
    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_STATE && h.bytes == 32u);
    const NvnOpBindState* got = p;
    CHECK(got->kind == NVN_STATE_MULTISAMPLE && got->words == 5);
    CHECK(got->w[0] == 5 && got->w[1] == 0x88888888u && got->w[4] == 0x88888888u);
    nvn_oplist_free(&l);
}

static void test_draw_path_ops(void) {
    CHECK(NVN_OP_DRAW > NVN_OP_BIND_STATE && NVN_OP_SET_RASTER < NVN_OP_COUNT);
    NvnOpList l = {0};
    NvnOpDraw draw = {NVN_DRAW_ELEMENTS_INSTANCED, 4, 1, 36, 0x400001000ull, 0, 7, 2, 3};
    NvnOpDispatch dispatch = {8, 4, 2, 0};
    NvnOpBindProgram program = {.program = 0x400002000ull, .stage_mask = 0x21u};
    NvnOpRaster raster = {NVN_RASTER_LINE_WIDTH, {0x40000000u, 0, 0}};
    CHECK(nvn_oplist_append(&l, NVN_OP_DRAW, &draw, sizeof draw));
    CHECK(nvn_oplist_append(&l, NVN_OP_DISPATCH, &dispatch, sizeof dispatch));
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_PROGRAM, &program, sizeof program));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_RASTER, &raster, sizeof raster));
    CHECK(l.ops == 4);

    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_DRAW && h.bytes == 40u);
    const NvnOpDraw* got_draw = p;
    CHECK(got_draw->kind == NVN_DRAW_ELEMENTS_INSTANCED && got_draw->mode == 4 && got_draw->type == 1);
    CHECK(got_draw->count == 36 && got_draw->address == 0x400001000ull);
    CHECK(got_draw->first == 0 && got_draw->base_vertex == 7);
    CHECK(got_draw->base_instance == 2 && got_draw->instances == 3);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_DISPATCH && h.bytes == 16u);
    const NvnOpDispatch* got_dispatch = p;
    CHECK(got_dispatch->x == 8 && got_dispatch->y == 4 && got_dispatch->z == 2 && got_dispatch->reserved == 0);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_PROGRAM && h.bytes == 736u);
    const NvnOpBindProgram* got_program = p;
    CHECK(got_program->program == 0x400002000ull && got_program->stage_mask == 0x21u);
    CHECK(got_program->program_mask == 0u && got_program->word_count == 0u && got_program->words[0] == 0u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_SET_RASTER && h.bytes == 16u);
    const NvnOpRaster* got_raster = p;
    CHECK(got_raster->which == NVN_RASTER_LINE_WIDTH && got_raster->w[0] == 0x40000000u && got_raster->w[2] == 0);
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p));
    nvn_oplist_free(&l);
}

static void test_vertex_state_and_depth_ops(void) {
    CHECK(NVN_OP_BIND_VERTEX_ATTRIB > NVN_OP_SET_RASTER && NVN_OP_SET_DEPTH_RANGE < NVN_OP_COUNT);
    NvnOpList l = {0};
    NvnOpBindVertexAttrib attrib;
    memset(&attrib, 0, sizeof attrib);
    attrib.count = 2;
    attrib.w[0] = 0x04400040u;
    attrib.w[1] = 0x00000025u;
    NvnOpBindVertexStream stream;
    memset(&stream, 0, sizeof stream);
    stream.count = 2;
    stream.stride[0] = 0x123u;
    stream.stride[1] = 0x456u;
    stream.divisor[1] = 3;
    NvnOpDepthRange depth = {5, 1, 0x3E800000u, 0x3F000000u};
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_VERTEX_ATTRIB, &attrib, sizeof attrib));
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_VERTEX_STREAM, &stream, sizeof stream));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_DEPTH_RANGE, &depth, sizeof depth));
    CHECK(l.ops == 3);

    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_VERTEX_ATTRIB && h.bytes == 72u);
    const NvnOpBindVertexAttrib* got_attrib = p;
    CHECK(got_attrib->count == 2 && got_attrib->w[0] == 0x04400040u && got_attrib->w[1] == 0x00000025u);
    CHECK(got_attrib->w[2] == 0);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_VERTEX_STREAM && h.bytes == 136u);
    const NvnOpBindVertexStream* got_stream = p;
    CHECK(got_stream->count == 2 && got_stream->stride[0] == 0x123u && got_stream->stride[1] == 0x456u);
    CHECK(got_stream->divisor[0] == 0 && got_stream->divisor[1] == 3 && got_stream->stride[2] == 0);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_SET_DEPTH_RANGE && h.bytes == 16u);
    const NvnOpDepthRange* got_depth = p;
    CHECK(got_depth->index == 5 && got_depth->depth_mode == 1);
    CHECK(got_depth->near_bits == 0x3E800000u && got_depth->far_bits == 0x3F000000u);
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p));
    nvn_oplist_free(&l);
}

static void test_gpu_ops(void) {
    CHECK(NVN_OP_CLEAR_DEPTH_STENCIL > NVN_OP_SET_DEPTH_RANGE);
    CHECK(NVN_OP_BIND_IMAGE < NVN_OP_COUNT);
    NvnOpList l = {0};
    NvnOpClearDepthStencil ds = {0x3F800000u, 1u, 0x2Au, 0xFFu};
    NvnOpClearTexture clear;
    memset(&clear, 0, sizeof clear);
    clear.target.texture = 0x400001000ull;
    clear.target.generation = 3;
    clear.target.has_view = 1;
    clear.target.view.base_level = 2;
    clear.target.view.base_layer = 4;
    clear.kind = NVN_CLEAR_KIND_INT;
    clear.mask = 0xFu;
    for (uint32_t i = 0; i < 6; i++) clear.region[i] = 10u + i;
    for (uint32_t i = 0; i < 4; i++) clear.color[i] = 0x100u + i;
    NvnOpCopyTexture copy;
    memset(&copy, 0, sizeof copy);
    copy.dst.texture = 0x400002000ull;
    copy.dst.generation = 4;
    copy.dst.has_view = 1;
    copy.dst.view.base_level = 1;
    copy.src.texture = 0x400003000ull;
    copy.src.generation = 5;
    copy.flags = 0x15u;
    for (uint32_t i = 0; i < 6; i++) {
        copy.dst_region[i] = 20u + i;
        copy.src_region[i] = 30u + i;
    }
    NvnOpReportCounter report = {NVN_REPORT_TYPE_MAX, 0, 0x412bbbfe0ull};
    NvnOpBindImage images;
    memset(&images, 0, sizeof images);
    images.stage = 4;
    images.first = 7;
    images.count = 2;
    images.handle[0] = 0x100000001ull;
    images.handle[1] = 0x200000002ull;
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_DEPTH_STENCIL, &ds, sizeof ds));
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_TEXTURE, &clear, sizeof clear));
    CHECK(nvn_oplist_append(&l, NVN_OP_COPY_TEXTURE, &copy, sizeof copy));
    CHECK(nvn_oplist_append(&l, NVN_OP_REPORT_COUNTER, &report, sizeof report));
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_IMAGE, &images, sizeof images));
    CHECK(l.ops == 5);

    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_CLEAR_DEPTH_STENCIL && h.bytes == sizeof ds);
    const NvnOpClearDepthStencil* got_ds = p;
    CHECK(got_ds->depth_bits == 0x3F800000u && got_ds->depth_enable == 1u);
    CHECK(got_ds->stencil == 0x2Au && got_ds->stencil_mask == 0xFFu);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_CLEAR_TEXTURE && h.bytes == sizeof clear);
    const NvnOpClearTexture* got_clear = p;
    CHECK(got_clear->target.texture == 0x400001000ull && got_clear->target.generation == 3);
    CHECK(got_clear->target.has_view == 1 && got_clear->target.view.base_level == 2);
    CHECK(got_clear->target.view.base_layer == 4 && got_clear->kind == NVN_CLEAR_KIND_INT);
    CHECK(got_clear->mask == 0xFu && got_clear->region[5] == 15u && got_clear->color[3] == 0x103u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_COPY_TEXTURE && h.bytes == sizeof copy);
    const NvnOpCopyTexture* got_copy = p;
    CHECK(got_copy->dst.texture == 0x400002000ull && got_copy->dst.generation == 4);
    CHECK(got_copy->src.texture == 0x400003000ull && got_copy->src.generation == 5);
    CHECK(got_copy->flags == 0x15u && got_copy->dst_region[5] == 25u && got_copy->src_region[5] == 35u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_REPORT_COUNTER && h.bytes == sizeof report);
    const NvnOpReportCounter* got_report = p;
    CHECK(got_report->type == NVN_REPORT_TYPE_MAX && got_report->address == 0x412bbbfe0ull);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_BIND_IMAGE && h.bytes == sizeof images);
    const NvnOpBindImage* got_images = p;
    CHECK(got_images->stage == 4 && got_images->first == 7 && got_images->count == 2);
    CHECK(got_images->handle[0] == 0x100000001ull && got_images->handle[1] == 0x200000002ull);
    CHECK(got_images->handle[2] == 0);
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p));
    nvn_oplist_free(&l);
}

static void test_u2_event_and_sync_ops(void) {
    CHECK(NVN_OP_SIGNAL_EVENT < NVN_OP_COUNT && NVN_OP_WAIT_SYNC < NVN_OP_COUNT);
    NvnOpList l = {0};
    NvnOpSignalEvent signal = {0x400001000ull, 9u, NVN_EVENT_SIGNAL_WRITE, 0x1234u, 1u};
    NvnOpWaitEvent wait = {0x400001000ull, 9u, NVN_EVENT_WAIT_EQUAL, 0x1234u, 0u};
    NvnOpReportValue value = {0xABCDEF01u, 0u, 0x400002000ull};
    NvnOpFenceSync fence = {0x400003000ull, 4u, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0u};
    NvnOpWaitSync wait_sync = {0x400003000ull, 4u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SIGNAL_EVENT, &signal, sizeof signal));
    CHECK(nvn_oplist_append(&l, NVN_OP_WAIT_EVENT, &wait, sizeof wait));
    CHECK(nvn_oplist_append(&l, NVN_OP_REPORT_VALUE, &value, sizeof value));
    CHECK(nvn_oplist_append(&l, NVN_OP_FENCE_SYNC, &fence, sizeof fence));
    CHECK(nvn_oplist_append(&l, NVN_OP_WAIT_SYNC, &wait_sync, sizeof wait_sync));
    CHECK(l.ops == 5);

    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_SIGNAL_EVENT && h.bytes == sizeof signal);
    const NvnOpSignalEvent* got_signal = p;
    CHECK(got_signal->event == 0x400001000ull && got_signal->generation == 9u && got_signal->mode == 0u);
    CHECK(got_signal->value == 0x1234u && got_signal->location == 1u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_WAIT_EVENT && h.bytes == sizeof wait);
    const NvnOpWaitEvent* got_wait = p;
    CHECK(got_wait->event == 0x400001000ull && got_wait->generation == 9u && got_wait->mode == 0u);
    CHECK(got_wait->value == 0x1234u && got_wait->reserved == 0u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_REPORT_VALUE && h.bytes == sizeof value);
    const NvnOpReportValue* got_value = p;
    CHECK(got_value->value == 0xABCDEF01u && got_value->address == 0x400002000ull);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_FENCE_SYNC && h.bytes == sizeof fence);
    const NvnOpFenceSync* got_fence = p;
    CHECK(got_fence->sync == 0x400003000ull && got_fence->generation == 4u && got_fence->condition == 0u);

    CHECK(nvn_oplist_next(&l, &pos, &h, &p) && h.code == NVN_OP_WAIT_SYNC && h.bytes == sizeof wait_sync);
    const NvnOpWaitSync* got_wait_sync = p;
    CHECK(got_wait_sync->sync == 0x400003000ull && got_wait_sync->generation == 4u && got_wait_sync->reserved == 0u);
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p));
    nvn_oplist_free(&l);
}

static void test_growth(void) {
    NvnOpList l = {0};
    for (uint32_t i = 0; i < 5000; i++) {
        NvnOpBarrier b = {i, 0};
        CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &b, sizeof b));
    }
    size_t pos = 0;
    NvnOpHeader h;
    const void* p;
    for (uint32_t i = 0; i < 5000; i++) {
        CHECK(nvn_oplist_next(&l, &pos, &h, &p) && ((const NvnOpBarrier*)p)->bits == i);
    }
    CHECK(!nvn_oplist_next(&l, &pos, &h, &p));
    nvn_oplist_free(&l);
}

static uint64_t publish_call(NvnListStore* s, const uint64_t* targets, unsigned count) {
    NvnOpList l = {0};
    for (unsigned i = 0; i < count; i++) {
        NvnOpCall call = {targets[i]};
        CHECK(nvn_list_ref(s, targets[i]));
        CHECK(nvn_oplist_append(&l, NVN_OP_CALL, &call, sizeof call));
    }
    uint64_t handle = nvn_list_publish(s, &l);
    CHECK(handle);
    return handle;
}

static void test_handles(void) {
    NvnListStore s = {0};
    NvnOpList l = {0};
    NvnOpBarrier b = {1, 0};
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &b, sizeof b));
    uint64_t h = nvn_list_publish(&s, &l);
    CHECK(h && !(h & 1) && (h >> 32) && !l.data); /* never 0, bit 0 clear, a generation, the list moved */
    const NvnOpList* found = nvn_list_find(&s, h);
    CHECK(found && found->ops == 1);
    CHECK(!nvn_list_find(&s, 0) && !nvn_list_find(&s, h | 1) && !nvn_list_find(&s, h & 0xFFFFFFFFull));
    CHECK(!nvn_list_find(&s, h + 2) && !nvn_list_find(&s, (h & 0xFFFFFFFFull) | (5ull << 32)));
    CHECK(!nvn_list_find(&s, 0xFFFFFFFE00000000ull | (h & 0xFFFFFFFFull)));
    CHECK(nvn_list_live(&s) == 1);

    CHECK(nvn_list_ref(&s, h));
    nvn_list_unref(&s, h);
    CHECK(nvn_list_find(&s, h)); /* one reference left */
    nvn_list_unref(&s, h);
    CHECK(!nvn_list_find(&s, h) && !nvn_list_ref(&s, h) && nvn_list_live(&s) == 0);
    nvn_list_unref(&s, h); /* a dead handle is ignored */

    /* the slot is used again with a new generation: the old handle stays dead */
    NvnOpList l2 = {0};
    CHECK(nvn_oplist_append(&l2, NVN_OP_BARRIER, &b, sizeof b));
    uint64_t h2 = nvn_list_publish(&s, &l2);
    CHECK(h2 && (h2 & 0xFFFFFFFFull) == (h & 0xFFFFFFFFull) && h2 != h);
    CHECK(!nvn_list_find(&s, h) && nvn_list_find(&s, h2));
    nvn_list_store_free(&s);
}

/* the store grows while a replay holds a list pointer: the pointer must stay valid (the GPU thread reads it) */
static void test_find_pointer_stable(void) {
    NvnListStore s = {0};
    NvnOpBarrier b = {1, 0};
    NvnOpList first = {0};
    CHECK(nvn_oplist_append(&first, NVN_OP_BARRIER, &b, sizeof b));
    uint64_t h = nvn_list_publish(&s, &first);
    const NvnOpList* held = nvn_list_find(&s, h);
    CHECK(held && held->ops == 1);
    for (uint32_t i = 0; i < 300; i++) { /* several growths of the slot array */
        NvnOpList l = {0};
        CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &b, sizeof b));
        CHECK(nvn_list_publish(&s, &l));
    }
    CHECK(nvn_list_find(&s, h) == held && held->ops == 1);
    nvn_list_store_free(&s);
}

static void test_call_release(void) {
    NvnListStore s = {0};
    NvnOpList la = {0};
    NvnOpBarrier b = {7, 0};
    CHECK(nvn_oplist_append(&la, NVN_OP_BARRIER, &b, sizeof b));
    uint64_t a = nvn_list_publish(&s, &la);
    uint64_t twice[2] = {a, a};
    uint64_t c = publish_call(&s, twice, 2); /* c names a two times */
    CHECK(nvn_list_live(&s) == 2);
    nvn_list_unref(&s, a); /* the owner's reference goes: c still holds two */
    CHECK(nvn_list_find(&s, a) && nvn_list_live(&s) == 2);
    nvn_list_unref(&s, c); /* c goes and gives both references of a back */
    CHECK(!nvn_list_find(&s, a) && !nvn_list_find(&s, c) && nvn_list_live(&s) == 0 && !s.leaked);
    nvn_list_store_free(&s);
}

static void test_deep_chain(void) {
    enum { DEPTH = 20000 };
    NvnListStore s = {0};
    NvnOpList l0 = {0};
    CHECK(nvn_oplist_append(&l0, NVN_OP_BARRIER, NULL, 0));
    uint64_t top = nvn_list_publish(&s, &l0);
    for (int i = 0; i < DEPTH; i++) {
        uint64_t next = publish_call(&s, &top, 1);
        nvn_list_unref(&s, top); /* the new list holds the old one now */
        top = next;
    }
    CHECK(nvn_list_live(&s) == DEPTH + 1);
    nvn_list_unref(&s, top); /* the whole chain goes without a recursion */
    CHECK(nvn_list_live(&s) == 0 && !s.leaked);
    nvn_list_store_free(&s);
}

static void test_many_lists(void) {
    enum { N = 1000 };
    NvnListStore s = {0};
    static uint64_t handles[N];
    for (int i = 0; i < N; i++) {
        NvnOpList l = {0};
        NvnOpBarrier b = {(uint32_t)i, 0};
        CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &b, sizeof b));
        handles[i] = nvn_list_publish(&s, &l);
        CHECK(handles[i]);
    }
    for (int i = 0; i < N; i++) {
        const NvnOpList* l = nvn_list_find(&s, handles[i]);
        size_t pos = 0;
        NvnOpHeader h;
        const void* p;
        CHECK(l && nvn_oplist_next(l, &pos, &h, &p) && ((const NvnOpBarrier*)p)->bits == (uint32_t)i);
    }
    for (int i = 0; i < N; i += 2) nvn_list_unref(&s, handles[i]);
    CHECK(nvn_list_live(&s) == N / 2);
    nvn_list_store_free(&s);
}

static void test_oplist_copy(void) {
    NvnListStore s = {0};
    NvnOpList inner = {0}, src = {0}, dst = {0};
    NvnOpBarrier barrier = {7u, 0};
    NvnOpClearColor color = {1u, 3u, {1u, 2u, 3u, 4u}};
    CHECK(nvn_oplist_append(&inner, NVN_OP_BARRIER, &barrier, sizeof barrier));
    uint64_t inner_handle = nvn_list_publish(&s, &inner);
    CHECK(inner_handle && nvn_list_live(&s) == 1);
    CHECK(nvn_oplist_append(&src, NVN_OP_CLEAR_COLOR, &color, sizeof color));
    CHECK(nvn_list_ref(&s, inner_handle)); /* as CallCommands does for the CALL it records */
    NvnOpCall call = {inner_handle};
    CHECK(nvn_oplist_append(&src, NVN_OP_CALL, &call, sizeof call));
    CHECK(nvn_oplist_append(&src, NVN_OP_BARRIER, &barrier, sizeof barrier));
    uint64_t src_handle = nvn_list_publish(&s, &src);
    CHECK(src_handle);

    uint64_t failed = 1;
    CHECK(nvn_oplist_copy(&dst, nvn_list_find(&s, src_handle), &s, &failed) && !failed);
    size_t pa = 0, pb = 0;
    NvnOpHeader ha, hb;
    const void* x;
    const void* y;
    uint32_t ops = 0;
    for (;;) {
        int a = nvn_oplist_next(nvn_list_find(&s, src_handle), &pa, &ha, &x);
        int b = nvn_oplist_next(&dst, &pb, &hb, &y);
        CHECK(a == b);
        if (!a) break;
        CHECK(ha.code == hb.code && ha.bytes == hb.bytes && !memcmp(x, y, ha.bytes));
        ops++;
    }
    CHECK(ops == 3);

    /* The copied CALL holds its own reference, so the source can go. */
    nvn_list_unref(&s, src_handle);
    CHECK(nvn_list_find(&s, inner_handle) != NULL && nvn_list_live(&s) == 1);
    uint64_t dst_handle = nvn_list_publish(&s, &dst);
    nvn_list_unref(&s, inner_handle); /* the owner's reference of the inner list */
    CHECK(nvn_list_find(&s, inner_handle) != NULL);
    nvn_list_unref(&s, dst_handle);
    CHECK(!nvn_list_find(&s, inner_handle) && nvn_list_live(&s) == 0 && !s.leaked);
    nvn_list_store_free(&s);
}

int main(void) {
    test_append_next();
    test_append_refused();
    test_bind_texture();
    test_bind_buffer();
    test_bind_state();
    test_draw_path_ops();
    test_vertex_state_and_depth_ops();
    test_gpu_ops();
    test_u2_event_and_sync_ops();
    test_growth();
    test_handles();
    test_find_pointer_stable();
    test_call_release();
    test_oplist_copy();
    test_deep_chain();
    test_many_lists();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_cmdlist: ok\n");
    return 0;
}
