/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_PARAMS_H
#define SNX_NVN_PARAMS_H
#include <stdint.h>

#define NVN_WINDOW_MAX_TEXTURES 4u
#define NVN_WINDOW_NO_IMAGE (-1)
#define NVN_WINDOW_INVALID_INDEX UINT32_MAX
typedef enum { NVN_IMAGE_FREE = 0, NVN_IMAGE_ACQUIRED = 1, NVN_IMAGE_PRESENTED = 2 } NvnImageState;
typedef enum { NVN_SYNC_WAIT_SIGNALED = 0, NVN_SYNC_WAIT_SATISFIED = 1,
               NVN_SYNC_WAIT_TIMEOUT = 2, NVN_SYNC_WAIT_FAILED = 3 } NvnSyncWaitResult;
typedef struct {
    uint64_t device, native_window, textures;
    uint32_t texture_count, interval, active_texture_count;
} NvnWindowBuilder;
typedef struct { uint32_t x, y, width, height; } NvnWindowCrop;
typedef enum {
    NVN_SYNC_UNSIGNALED = 0, NVN_SYNC_SIGNALED = 1, NVN_SYNC_FENCED = 2,
    NVN_SYNC_PENDING = 3 /* a token in a pending frame: no syncpoint value yet */
} NvnSyncState;
/* nvnQueueFenceSync condition: the only value that the game used. The job's syncpoint increment follows its work. */
#define NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE 0u
void nvn_window_builder_defaults(NvnWindowBuilder* b);
void nvn_window_builder_set_device(NvnWindowBuilder* b, uint64_t device);
void nvn_window_builder_set_native_window(NvnWindowBuilder* b, uint64_t native_window);
void nvn_window_builder_set_textures(NvnWindowBuilder* b, uint32_t count, uint64_t textures);
void nvn_window_builder_set_interval(NvnWindowBuilder* b, uint32_t interval);
void nvn_window_builder_set_active_textures(NvnWindowBuilder* b, uint32_t count);
/* The host keeps the interval itself. The library stores interval - 1 and its getter adds 1. */
uint32_t nvn_window_builder_present_interval(const NvnWindowBuilder* b);
/* The library setter has no range check: the active texture count is every 32-bit value. */
uint32_t nvn_window_active_textures(uint32_t count);
int nvn_window_initialize_check(uint32_t texture_count);
int32_t nvn_window_pick_image(const uint8_t* states, uint32_t count, int32_t last);
/* 1 when at least one image of the window waits for its frame job. */
int nvn_window_has_presented(const uint8_t* states, uint32_t count);
/* The address of one texture slot of the builder. The library compares the count and the index signed and reads with a
   sign-extended 8-byte step, so a negative index is not refused. 1 with *at written, or 0 when the index is at or
   above the count. Pure. */
int nvn_window_texture_slot(uint64_t textures, uint32_t texture_count, int32_t index, uint64_t* at);
/* The refusal reason of nvnWindowAcquireTexture and nvnQueueAcquireTexture, in the order of the checks. recheck is 1
   after the pace and after each wait, where the queue is not checked again. NULL when the call may go on. Pure. */
const char* nvn_window_acquire_refusal(int recheck, int queue_ok, int window_ok, int sync_ok, int index_writable);
/* Marks the picked window image acquired and keeps it as the last index. */
void nvn_window_acquire_take(uint8_t* states, int32_t* last_index, int32_t index);
/* nvnSyncWait of the game: this timeout waits without limit. */
#define NVN_SYNC_WAIT_NO_LIMIT UINT64_MAX
typedef enum {
    NVN_SYNC_ACTION_SIGNALED = 0, NVN_SYNC_ACTION_WAIT_FRAME, NVN_SYNC_ACTION_WAIT_FENCE,
    NVN_SYNC_ACTION_REFUSE
} NvnSyncAction;
/* What nvnSyncWait does at once: return 0, wait for the frame's post or for the syncpoint, or return 3. reached is
   the answer for a FENCED sync's value. Pure. */
NvnSyncAction nvn_sync_wait_action(NvnSyncState state, int reached);
/* The value of a fence on an empty pending frame: the last posted value, or the syncpoint value when the queue never
   posted. Pure. */
uint32_t nvn_sync_fence_value(int has_posted, uint32_t last_posted, uint32_t syncpoint_now);
/* The limit of a wait in host nanoseconds. */
typedef struct {
    uint64_t deadline_ns;
    int has_deadline; /* 0: the game gave NVN_SYNC_WAIT_NO_LIMIT */
} NvnWaitDeadline;
NvnWaitDeadline nvn_wait_deadline(uint64_t timeout_ns, uint64_t now_ns);
/* The time left for a wait: -1 without limit, 0 at or after the deadline. */
int64_t nvn_wait_remaining_ns(NvnWaitDeadline limit, uint64_t now_ns);
NvnWindowCrop nvn_window_crop(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                              uint32_t texture_width, uint32_t texture_height);

/* nvnCommandBufferGetControlMemorySize: the last control chunk, with the reserve that the library keeps at the end of
   a control memory chunk. Before any AddControlMemory the library returns the reserve alone. */
#define NVN_CONTROL_MEMORY_RESERVE 0x10u
uint64_t nvn_control_memory_size(uint64_t begin, uint64_t end, uint32_t adds);

/* The pure parts of the host's device, queue and pool functions (nvn-design.md, 3b-3). The game's NVN library computes
   these from device fields that it fills at DeviceInitialize. The host has no device object, so it uses the values that
   a run of the library showed (the "3b-2 Result" and "3b-3 Result" sections). */

/* The library answers a `what` above this with 0 (the default of the switch at d831f68). Below it, a table gives the value. */
#define NVN_DEVICE_INTEGER_LIMIT 0x62u
/* What nvnDeviceGetInteger answers for `what`. 0 when the host has no value for it (the library has one, no run asked). */
int nvn_device_integer(uint32_t what, int32_t* value);

/* nvnDeviceGetTimestampInNanoseconds: the GPU ticks of a counter report as nanoseconds. Same integer steps as the
   library, so the results match to the last digit and a large tick count does not overflow. */
uint64_t nvn_timestamp_ns(uint64_t ticks);

/* nvnDeviceRegisterFastClearColor accepts the float format range 10..156. The Colori and Colorui forms accept 11..120.
   Outside the range the library returns 0 without a driver call. The depth form has no format check. */
#define NVN_FAST_CLEAR_COLOR_FORMAT_MIN  10u
#define NVN_FAST_CLEAR_COLOR_FORMAT_MAX  156u
#define NVN_FAST_CLEAR_COLORI_FORMAT_MIN 11u
#define NVN_FAST_CLEAR_COLORI_FORMAT_MAX 120u
typedef enum { NVN_FAST_CLEAR_KIND_FLOAT = 0, NVN_FAST_CLEAR_KIND_INT } NvnFastClearKind;
int nvn_fast_clear_color_format_ok(NvnFastClearKind kind, uint32_t format);

/* The NVN texture and image handles (sdk d82d0d0, d82d050). The library masks no index: a texture index above 0xFFFFF
   spills into the sampler field, and index bit 31 is sign extended. Both properties are kept. */
#define NVN_HANDLE_SAMPLER_SHIFT 20u
#define NVN_TEXTURE_HANDLE_TAG   0x100000000ull
uint64_t nvn_texture_handle(uint32_t texture_index, uint32_t sampler_index);
uint64_t nvn_image_handle(uint32_t image_index);
/* nvnDeviceGetSeparateSamplerHandle: the sampler field alone, sign extended and shifted, with no tag. */
uint64_t nvn_separate_sampler_handle(uint32_t sampler_index);

/* The four kinds of buffer bind of a command buffer. A uniform and a vertex bind are active with a nonzero size, a
   transform feedback bind with a nonzero address, and a storage bind is always active. */
typedef enum { NVN_BIND_UNIFORM = 0, NVN_BIND_STORAGE, NVN_BIND_VERTEX, NVN_BIND_TRANSFORM_FEEDBACK } NvnBindKind;
int nvn_bind_buffer_active(NvnBindKind kind, uint64_t addr, uint64_t size);

/* The kinds of the draw op. The order is the kind word. The library copies the arguments unchanged. */
typedef enum {
    NVN_DRAW_ARRAYS = 0,
    NVN_DRAW_ARRAYS_INSTANCED,
    NVN_DRAW_ELEMENTS,
    NVN_DRAW_ELEMENTS_BASE_VERTEX,
    NVN_DRAW_ELEMENTS_INSTANCED,
    NVN_DRAW_KIND_COUNT
} NvnDrawKind;

/* Writes the words of one draw as the library does. kind is NvnDrawKind. Returns the word count, or 0 for a refusal
   (kind out of range, out NULL), with nothing written. *uses_constants is set to 1 when the words need the driver
   constants buffer (macro 0xE20), else 0. */
#define NVN_DRAW_WORDS_MAX 18u
uint32_t nvn_draw_words(uint32_t kind, uint32_t mode, uint32_t type, uint32_t count, uint64_t address, uint32_t first,
                        uint32_t base_vertex, uint32_t base_instance, uint32_t instances, uint32_t* out,
                        int* uses_constants);

/* The kinds of the raster op. The order is the kind word. */
typedef enum {
    NVN_RASTER_DEPTH_BOUNDS = 0,
    NVN_RASTER_DISCARD,
    NVN_RASTER_POLYGON_OFFSET_CLAMP,
    NVN_RASTER_DEPTH_CLAMP,
    NVN_RASTER_LINE_WIDTH,
    NVN_RASTER_POINT_SIZE,
    NVN_RASTER_KIND_COUNT
} NvnRasterKind;

/* The Maxwell methods that a raster op writes (analyst-g4-draw-1.md). */
#define NVN_RASTER_METHOD_DEPTH_BOUNDS         0x66Fu
#define NVN_RASTER_METHOD_DEPTH_BOUNDS_MIN     0x3E7u
#define NVN_RASTER_METHOD_DEPTH_BOUNDS_MAX     0x3E8u
#define NVN_RASTER_METHOD_DISCARD              0x0DFu
#define NVN_RASTER_METHOD_POLYGON_OFFSET       0x55Bu
#define NVN_RASTER_METHOD_POLYGON_OFFSET_UNITS 0x56Fu
#define NVN_RASTER_METHOD_POLYGON_OFFSET_CLAMP 0x61Fu
#define NVN_RASTER_METHOD_DEPTH_CLAMP          0x64Fu
#define NVN_RASTER_METHOD_LINE_WIDTH           0x4ECu
#define NVN_RASTER_METHOD_LINE_WIDTH_ALIAS     0x4EDu
#define NVN_RASTER_METHOD_POINT_SIZE           0x546u
/* SetPointSize (d84a600) writes 0 here after the size, BindTexture writes the same word under a flag. Not decoded. */
#define NVN_RASTER_METHOD_POINT_SIZE_FOLLOW    0x68Bu
#define NVN_RASTER_DEPTH_CLAMP_NEAR            0x101Au
#define NVN_RASTER_DEPTH_CLAMP_FAR             0x181Du
#define NVN_RASTER_METHODS_MAX 3u

/* One method and value pair of a raster op. */
typedef struct { uint32_t method, value; } NvnRasterMethod;

/* The pairs that the library writes for a raster op, in order. 0 for a kind the library does not write
   (out and count unchanged). Pure. */
int nvn_raster_methods(NvnRasterKind which, const uint32_t w[3], NvnRasterMethod out[NVN_RASTER_METHODS_MAX],
                       uint32_t* count);

/* The Maxwell methods that a depth range writes (analyst-g5-vertex-1.md). */
#define NVN_DEPTH_RANGE_METHOD_FRACTION   0x282u
#define NVN_DEPTH_RANGE_METHOD_TRANSLATE  0x285u
#define NVN_DEPTH_RANGE_METHOD_MIN        0x302u
#define NVN_DEPTH_RANGE_METHOD_MAX        0x303u
#define NVN_DEPTH_RANGE_METHOD_STRIDE     8u
#define NVN_DEPTH_RANGE_CLIP_STRIDE       4u
#define NVN_DEPTH_RANGE_METHODS_MAX       4u

typedef struct { uint32_t method, value; } NvnDepthRangeMethod;

/* The four method and value pairs of one depth range, in the order the library writes them. depth_mode 1 keeps the
   values, other modes halve the difference and the sum. Pure. */
void nvn_depth_range_methods(uint32_t index, uint32_t depth_mode, float near_value, float far_value,
                             NvnDepthRangeMethod out[NVN_DEPTH_RANGE_METHODS_MAX]);

/* The state objects of a command buffer and the argument kinds of the U4 draw state setters. The order is the kind
   word of the bind state op, so the six object kinds stay first. Only the object kinds have a state record. */
#define NVN_STATE_OBJECT_COUNT 6u
typedef enum {
    NVN_STATE_COLOR = 0,
    NVN_STATE_DEPTH_STENCIL,
    NVN_STATE_BLEND,
    NVN_STATE_POLYGON,
    NVN_STATE_MULTISAMPLE,
    NVN_STATE_CHANNEL_MASK,
    NVN_STATE_PATCH_SIZE,
    NVN_STATE_INNER_TESSELLATION,
    NVN_STATE_OUTER_TESSELLATION,
    NVN_STATE_PRIMITIVE_RESTART,
    NVN_STATE_RENDER_ENABLE, /* both setters write 0x556, so one kind keeps their order */
    NVN_STATE_COUNT
} NvnStateKind;

#define NVN_STATE_WORDS_MAX 5u /* the multisample bind reads word 0 and four sample location words */

/* The library stores these fields in plain words; the value is not masked, so its high bits spill as in the library. */
uint32_t nvn_state_insert(uint32_t word, uint32_t mask, uint32_t shift, uint32_t value);
uint32_t nvn_state_set_flag(uint32_t word, uint32_t bit, uint32_t value);

uint32_t nvn_blend_state_set_blend_func(uint32_t word, uint32_t src_color, uint32_t dst_color,
                                        uint32_t src_alpha, uint32_t dst_alpha);
uint32_t nvn_blend_state_set_blend_equation(uint32_t word, uint32_t rgb, uint32_t alpha);
uint32_t nvn_channel_mask_state_set_channel_mask(uint32_t word, uint32_t target, uint32_t r, uint32_t g,
                                                 uint32_t b, uint32_t a);
uint32_t nvn_color_state_set_blend_enable(uint32_t word, uint32_t target, uint32_t value);
uint32_t nvn_depth_stencil_state_set_stencil_func(uint32_t word, uint32_t face, uint32_t func);
uint32_t nvn_depth_stencil_state_set_stencil_op(uint32_t word, uint32_t face, uint32_t fail, uint32_t zfail,
                                                uint32_t zpass);
uint32_t nvn_vertex_attrib_state_set_format(uint32_t word, uint32_t format, uint32_t relative_offset);
uint32_t nvn_vertex_attrib_state_set_stream_index(uint32_t word, uint32_t stream_index);
/* The library getters (d83af80, d83afa0): format bits 21-30, relative offset bits 7-20, stream index bits 0-4. */
void nvn_vertex_attrib_state_get_format(uint32_t word, uint32_t* format, uint64_t* relative_offset);
uint32_t nvn_vertex_attrib_state_get_stream_index(uint32_t word);

/* ResetCounter (d856b40) writes CLEAR_REPORT_VALUE with a word of a table that the counter type indexes. 1 and the
   word for a type of 0 to 16. The library has no range check. 0 for any other type. */
#define NVN_RESET_COUNTER_METHOD 0x54Cu
int nvn_reset_counter_word(uint32_t type, uint32_t* word);

void nvn_color_state_defaults(uint32_t* words);
void nvn_depth_stencil_state_defaults(uint32_t* words);
void nvn_blend_state_defaults(uint32_t* words);
void nvn_polygon_state_defaults(uint32_t* words);
void nvn_multisample_state_defaults(uint32_t* words);
void nvn_channel_mask_state_defaults(uint32_t* words);

uint32_t nvn_blend_state_get_blend_target(uint32_t word);
void nvn_blend_state_get_blend_func(uint32_t word, uint32_t out[4]);
void nvn_channel_mask_state_get_channel_mask(uint32_t word, uint32_t target, uint8_t out[4]);
uint32_t nvn_color_state_get_alpha_test(uint32_t word);
uint32_t nvn_color_state_get_blend_enable(uint32_t word, uint32_t target);
uint32_t nvn_depth_stencil_state_get_depth_func(uint32_t word);
uint32_t nvn_depth_stencil_state_get_depth_test_enable(uint32_t word);
uint32_t nvn_depth_stencil_state_get_depth_write_enable(uint32_t word);
uint32_t nvn_multisample_state_get_multisample_enable(uint32_t word);
uint32_t nvn_multisample_state_get_samples(uint32_t word);
uint32_t nvn_multisample_state_get_alpha_to_coverage_enable(uint32_t word);
uint32_t nvn_multisample_state_get_alpha_to_coverage_dither(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_modulation_mode(uint32_t word);
uint32_t nvn_multisample_state_get_raster_samples(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_to_color_enable(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_to_color_output(uint32_t word);
uint32_t nvn_polygon_state_get_cull_face(uint32_t word);
uint32_t nvn_polygon_state_get_front_face(uint32_t word);
uint32_t nvn_color_state_get_logic_op(uint32_t word);
uint32_t nvn_depth_stencil_state_get_stencil_test_enable(uint32_t word);
uint32_t nvn_depth_stencil_state_get_stencil_func(uint32_t word, uint32_t face);
uint32_t nvn_polygon_state_get_polygon_mode(uint32_t word);
uint32_t nvn_polygon_state_get_polygon_offset_enables(uint32_t word);
void nvn_depth_stencil_state_get_stencil_op(uint32_t word, uint32_t face, uint32_t out[3]);
void nvn_blend_state_get_blend_equation(uint32_t word, uint32_t* rgb, uint32_t* alpha);

/* The advanced blend fields of word 0. The setters clear only their own field, so a value with high bits spills as in
   the library. */
uint32_t nvn_blend_state_get_advanced_mode(uint32_t word);
uint32_t nvn_blend_state_get_advanced_overlap(uint32_t word);
uint32_t nvn_blend_state_get_advanced_premultiplied_src(uint32_t word);
uint32_t nvn_blend_state_get_advanced_normalized_dst(uint32_t word);
uint32_t nvn_blend_state_set_advanced_mode(uint32_t word, uint32_t value);
uint32_t nvn_blend_state_set_advanced_overlap(uint32_t word, uint32_t value);
uint32_t nvn_blend_state_set_advanced_premultiplied_src(uint32_t word, uint32_t value);
uint32_t nvn_blend_state_set_advanced_normalized_dst(uint32_t word, uint32_t value);

/* The sample locations of a multisample state (sdk d83b470..d83b560). The grid is a table of the count that the
   library takes as the larger of the two sample fields. */
uint32_t nvn_multisample_state_get_sample_locations_enable(uint32_t word);
uint32_t nvn_multisample_state_get_sample_locations_grid_enable(uint32_t word);
int nvn_multisample_state_sample_grid(uint32_t samples, uint32_t raster_samples, uint32_t* x, uint32_t* y);
uint8_t nvn_multisample_sample_location(float x, float y);
#define NVN_MULTISAMPLE_LOCATION_BYTES 16u /* the four sample location words of the record, guest bytes 4..19 */
/* 1 when count sample locations at index stay inside the record bytes. Pure. */
int nvn_multisample_locations_fit(uint32_t index, uint32_t count);

/* The pushbuffer words that the library writes for the command buffer bind functions (analyst-s2-A-1.md). Each builder
   writes the same words as the library. The caller supplies room for the named maximum, and the count is 0 on a refusal
   with nothing written. The host rules refuse a stage above 4, a slot outside its range, a polygon mode of 3, advanced
   blend and a vertex count above 16. */
#define NVN_BIND_COLOR_WORDS_MAX 5u
#define NVN_BIND_CHANNEL_MASK_WORDS_MAX 5u
#define NVN_BIND_DEPTH_STENCIL_WORDS_MAX 5u
#define NVN_BIND_POLYGON_WORDS_MAX 6u
#define NVN_BIND_BLEND_WORDS_MAX 10u
#define NVN_BIND_VERTEX_ATTRIB_WORDS_MAX 17u
#define NVN_BIND_VERTEX_STREAM_WORDS_MAX 64u
#define NVN_BIND_VERTEX_BUFFER_WORDS_MAX 6u
#define NVN_BIND_UNIFORM_BUFFER_WORDS_MAX 5u
#define NVN_BIND_STORAGE_BUFFER_WORDS_MAX 5u
#define NVN_BIND_TEXTURE_WORDS_MAX 6u
#define NVN_BIND_TEXTURES_WORDS_MAX 260u          /* three header words, one slot word, two words for each of 128 */
#define NVN_BIND_STORAGE_BUFFERS_WORDS_MAX 515u   /* three header words and four words for each of 128 entries */

uint32_t nvn_bind_color_words(uint32_t w0, uint32_t* out);
uint32_t nvn_bind_channel_mask_words(uint32_t w0, uint32_t* out);
uint32_t nvn_bind_depth_stencil_words(uint32_t w0, uint32_t w1, uint32_t* out);
uint32_t nvn_bind_polygon_words(uint32_t w0, uint32_t* out);
uint32_t nvn_bind_blend_words(uint32_t w0, uint32_t w1, uint32_t* out);
uint32_t nvn_bind_vertex_attrib_words(uint32_t count, const uint32_t* s, const uint32_t* fmt, uint32_t* out);
uint32_t nvn_bind_vertex_stream_words(uint32_t count, const uint32_t* stride, const uint32_t* divisor,
                                      uint32_t* out);
uint32_t nvn_bind_vertex_buffer_words(uint32_t index, uint64_t addr, uint64_t size, uint32_t* out);
uint32_t nvn_bind_uniform_buffer_words(uint32_t stage, uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out);
uint32_t nvn_bind_storage_buffer_words(uint32_t stage, uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out);
/* The plural forms of BindTextures and BindStorageBuffers: the entries are address and size pairs for the storage
   bind, and 64-bit handles for the texture bind. */
uint32_t nvn_bind_storage_buffers_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* entries,
                                        uint32_t* out);
uint32_t nvn_bind_texture_words(uint32_t stage, uint32_t slot, uint64_t handle, int descriptor_flag, uint32_t* out);
uint32_t nvn_bind_textures_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* handles,
                                 int descriptor_flag, uint32_t* out);

/* The update ops that the host sends as words: a report counter, an image bind of one or more handles, and a clear of
   the depth and stencil buffer (analyst-s4-4.md, sections 1, 4 and 5). The count is 0 on a refusal with nothing
   written. A clear with neither the depth nor the stencil enabled writes nothing and is not a refusal. */
#define NVN_REPORT_WORDS_MAX 21u
#define NVN_REPORT_VALUE_WORDS_MAX 5u
#define NVN_BIND_IMAGE_WORDS_MAX 96u
#define NVN_CLEAR_DEPTH_STENCIL_WORDS_MAX 12u
uint32_t nvn_report_counter_words(uint32_t type, uint64_t address, uint32_t* out);
/* ReportValue: the report of one value at the address, in the shape of a type 0 report. The library's trigger word
   (0x800003E0) is not sent: method 0x3E0 has no action in the host model. 0 for a null out. */
uint32_t nvn_report_value_words(uint32_t value, uint64_t address, uint32_t* out);
uint32_t nvn_bind_image_words(uint32_t stage, uint32_t first, uint32_t count, const uint64_t* handles,
                              int descriptor_flag, int plural, uint32_t* out);
int nvn_clear_depth_stencil_nothing(uint32_t depth_enable, uint32_t stencil_mask);
uint32_t nvn_clear_depth_stencil_words(uint32_t depth_bits, uint32_t depth_enable, uint32_t stencil,
                                       uint32_t stencil_mask, int zcull_clear, int use_macro_e26, uint32_t* out);

/* CopyBufferToBuffer and ClearBuffer (analyst-u3-A-1.md): the DMA words of the library. The copy splits at 0x3FFFFF
   bytes a chunk and returns 0 when cap cannot hold the chunks. The clear of size 0 writes nothing. The library markers
   (method 2 of subchannel 0) are left out. */
#define NVN_COPY_BUFFER_CHUNK_BYTES       0x3FFFFFull
#define NVN_COPY_BUFFER_WORDS_PER_CHUNK   9u
#define NVN_COPY_BUFFER_MAX_WORDS         288u /* 32 chunks; a larger copy is split by the replay */
#define NVN_CLEAR_BUFFER_WORDS_MAX        16u
uint32_t nvn_copy_buffer_words(uint64_t src, uint64_t dst, uint64_t size, uint32_t* out, uint32_t cap);
uint32_t nvn_clear_buffer_words(uint64_t dst, uint64_t size, uint32_t value, uint32_t* out);

/* UpdateUniformBuffer (analyst-u3-A-1.md): the size word rounds up to 256 with a minimum of 256. One op holds at most
   NVN_UPDATE_UNIFORM_DATA_WORDS words; the builder writes the 0x8E0 run, the 0x68B PipeNop and mode 3 data chunks of
   at most 127 words. 0 for a null out or data. */
#define NVN_UPDATE_UNIFORM_CHUNK_WORDS 127u
#define NVN_UPDATE_UNIFORM_DATA_WORDS  186u /* (768 - 24) / 4: one op payload after its 24-byte header */
#define NVN_UPDATE_UNIFORM_WORDS_MAX   (6u + 2u + NVN_UPDATE_UNIFORM_DATA_WORDS)
uint32_t nvn_update_uniform_size256(uint32_t size);
uint32_t nvn_update_uniform_words(uint64_t address, uint32_t size256, uint32_t offset, const uint32_t* data,
                                  uint32_t words, uint32_t* out);

/* Transform feedback (sdk d84b400 to d84b6e4). A buffer bind is the enable, address and size run of its slot, or an
   inline 0 there for address 0. Begin calls macro 0 with the control address. End disables the feedback and writes
   the four stream byte counts to address + 4 to + 16 with report semaphores. Pause with an address writes the End
   words and with address 0 only the disable. Resume writes only the enable and needs address 0. 0 for a bad slot or
   kind. */
#define NVN_XFB_BUFFER_COUNT 4u
#define NVN_BIND_XFB_WORDS_MAX 5u
#define NVN_XFB_WORDS_MAX 21u
typedef enum { NVN_XFB_BEGIN = 0, NVN_XFB_END, NVN_XFB_PAUSE, NVN_XFB_RESUME, NVN_XFB_KIND_COUNT } NvnXfbKind;
uint32_t nvn_bind_xfb_words(uint32_t slot, uint64_t addr, uint32_t size, uint32_t* out);
uint32_t nvn_xfb_words(uint32_t kind, uint64_t addr, uint32_t* out);

/* The draw state of the U4 tessellation, primitive restart and render enable setters (analyst-u4-A-1.md,
   analyst-u4-B-1.md). Each builder writes the library words for the arguments of one setter. 0 for an argument
   that has no library form. */
#define NVN_PATCH_SIZE_WORDS_MAX 1u
#define NVN_TESS_INNER_LEVEL_COUNT 2u
#define NVN_TESS_OUTER_LEVEL_COUNT 4u
#define NVN_TESS_INNER_LEVELS_WORDS_MAX 3u
#define NVN_TESS_OUTER_LEVELS_WORDS_MAX 5u
#define NVN_PRIMITIVE_RESTART_WORDS_MAX 3u
#define NVN_RENDER_ENABLE_WORDS_MAX 1u
#define NVN_RENDER_ENABLE_CONDITIONAL_WORDS_MAX 4u
uint32_t nvn_patch_size_words(uint32_t patches, uint32_t* out);
uint32_t nvn_inner_tessellation_levels_words(const uint32_t bits[2], uint32_t* out);
uint32_t nvn_outer_tessellation_levels_words(const uint32_t bits[4], uint32_t* out);
uint32_t nvn_primitive_restart_words(uint32_t enable, uint32_t index, uint32_t* out);
uint32_t nvn_render_enable_words(uint32_t enable, uint32_t* out);
#define NVN_RENDER_ENABLE_CONDITION_COUNT 2u /* the library writes a word for condition 0 and 1 only */
/* Render enable state words: form, then the enable (form VALUE) or condition, sync high, sync low (form CONDITIONAL). */
#define NVN_RENDER_ENABLE_FORM_VALUE 0u
#define NVN_RENDER_ENABLE_FORM_CONDITIONAL 1u
#define NVN_RENDER_ENABLE_STATE_WORDS 4u
uint32_t nvn_render_enable_conditional_words(uint32_t condition, uint64_t sync, uint32_t* out);
/* SetConservativeRasterDilate: clamp(ceil(4 * s), 0, 3); a NaN or a negative value gives 0. Pure. */
uint32_t nvn_conservative_raster_dilate_level(float dilate);
/* SetViewportSwizzles: the packed word of one entry, and 1 when every entry is the identity.
   MAXWELL3D_VIEWPORT_SWIZZLE_IDENTITY of maxwell/maxwell_3d_regs.h packs to 0x6420. Pure. */
#define NVN_VIEWPORT_SWIZZLE_ENTRY_BYTES 16u /* four u32 per entry */
uint32_t nvn_viewport_swizzle_word(const uint32_t entry[4]);
int nvn_viewport_swizzles_identity(const uint32_t* words, uint32_t count);
/* The 10-bit format index that the host uses to read the format word of one vertex attribute state. */
uint32_t nvn_vertex_attrib_format_index(uint32_t s);
/* The count to pass to a builder for a table of enabled flags: the highest set index plus one, or 0 when none is set. */
uint32_t nvn_highest_enabled_index(const uint8_t* enabled, uint32_t count);

/* The device words that nvnQueueBuilderGetQueueMemorySize reads (offsets in the names). */
typedef struct {
    uint32_t w8c, w94, w98, we0, we4, w1af4;
} NvnQueueDeviceWords;
extern const NvnQueueDeviceWords nvn_queue_device_words;

/* The host's copy of an nvnQueueBuilder. A size counts only when its has_ field is 1 (the setter ran). */
typedef struct {
    uint64_t device, memory;
    uint32_t flags, memory_size, priority;
    int32_t command, compute, control, flush_threshold;
    uint8_t has_command, has_compute, has_control, has_flush_threshold;
} NvnQueueBuilder;

#define NVN_QUEUE_PRIORITY_DEFAULT 100 /* what the library's SetDefaults stores */
void nvn_queue_builder_defaults(NvnQueueBuilder* b);
/* The bytes of memory that a queue with this builder needs: the answer of nvnQueueBuilderGetQueueMemorySize */
uint64_t nvn_queue_memory_size(const NvnQueueBuilder* b, const NvnQueueDeviceWords* dev);

/* One word of the queue init device image: its byte offset and its value. */
typedef struct { uint32_t offset, value; } NvnQueueInitWord;
#define NVN_QUEUE_INIT_DEVICE_WORD_COUNT 16u
/* The device image words of the queue init builders, in the order they are written. Returns the count, or 0 when
   max is below the count (nothing is written then). */
uint32_t nvn_queue_init_device_words(const NvnQueueDeviceWords* dev, uint32_t window_origin, uint32_t depth_mode,
                                     NvnQueueInitWord* out, uint32_t max);

#define NVN_POOL_CPU_NO_ACCESS 0x1u
#define NVN_POOL_CPU_UNCACHED 0x2u
#define NVN_POOL_CPU_CACHED 0x4u
#define NVN_POOL_GPU_NO_ACCESS 0x8u
#define NVN_POOL_GPU_UNCACHED 0x10u
#define NVN_POOL_GPU_CACHED 0x20u
#define NVN_POOL_PHYSICAL 0x100u
#define NVN_POOL_VIRTUAL 0x200u
#define NVN_POOL_IMPORT 0x20000000u
#define NVN_POOL_PUBLIC_FLAGS 0x01ffffffu
#define NVN_POOL_DEFAULT_FLAGS 0x21u
#define NVN_POOL_STORAGE_ALIGNMENT 0x1000ull
/* The host device uses the minimum page size. No NvRm device is constructed. */
#define NVN_POOL_PAGE_SIZE NVN_POOL_STORAGE_ALIGNMENT
#define NVN_POOL_GPU_BASE 0x400000000ull
/* A virtual pool maps pages of the library's page size (the NvRm device's 64 KiB), not the storage alignment. */
#define NVN_POOL_VIRTUAL_PAGE_SIZE 0x10000ull

typedef struct {
    uint64_t device, storage, size;
    uint32_t flags, handle;
    uint64_t optional[3];
} NvnMemoryPoolBuilder;

typedef enum {
    NVN_POOL_OK = 0,
    NVN_POOL_UNSUPPORTED_VIRTUAL,
    NVN_POOL_UNSUPPORTED_IMPORT,
    NVN_POOL_INVALID_STORAGE,
    NVN_POOL_INVALID_PAGE_SIZE
} NvnPoolResult;

void nvn_memory_pool_builder_defaults(NvnMemoryPoolBuilder* b);
/* Normalizes flags even on failure. Service-owned handles are not supported. */
NvnPoolResult nvn_memory_pool_params(const NvnMemoryPoolBuilder* b, uint64_t page_size, uint32_t* flags);
/* Pure bump allocation: no address reuse. Outputs stay unchanged on failure. End is exclusive. */
/* The byte offset of a descriptor inside its memory pool. Fails (0) when index is outside [0, count), or when the
   descriptor range [pool_offset, pool_offset + count * entry_bytes) does not fit in the memory pool. */
/* 1 when [offset, offset + size) lies inside a memory pool of pool_size bytes, without overflow. */
int nvn_pool_range_fits(uint64_t pool_size, uint64_t offset, uint64_t size);
/* 1 when a texture gets the pitch range of its pool, 0 for the block linear range (d83f418). */
int nvn_texture_uses_pitch_range(uint32_t texture_flags, uint32_t target);
typedef enum { NVN_BUFFER_OK = 0, NVN_BUFFER_PHYSICAL_POOL, NVN_BUFFER_OUT_OF_POOL } NvnBufferResult;
/* The library fails BufferInitialize only for a physical pool (flag 0x100). The range check is the host's: the library
   has none, and the replay reads the buffer's bytes from the pool. */
NvnBufferResult nvn_buffer_check(uint32_t pool_flags, uint64_t pool_size, uint64_t offset, uint64_t size);
int nvn_descriptor_offset(uint64_t pool_offset, uint32_t count, uint64_t memory_size, uint32_t entry_bytes,
                          int32_t index, uint64_t* offset);
uint64_t nvn_memory_pool_map(uint32_t flags, uint64_t storage);
uint32_t nvn_memory_pool_flags(uint32_t flags);

/* nvnMemoryPoolMapVirtual: requests of stride 0x28, at most 0x200 in one batch (the library's remap limit). */
#define NVN_POOL_MAP_VIRTUAL_BATCH 0x200u
#define NVN_POOL_MAP_VIRTUAL_REQUEST_BYTES 0x28u
/* One NVNmappingRequest: the physical pool (0 unmaps), the physical offset, the virtual offset, the size and the
   storage class. The host ignores the storage class. */
typedef struct {
    uint64_t physical_pool, physical_offset, virtual_offset, size;
    uint32_t storage_class;
} NvnMapVirtualRequest;
typedef enum {
    NVN_MAP_VIRTUAL_OK = 0,
    NVN_MAP_VIRTUAL_SKIP,       /* the size is 0 after the 64 KiB truncation: the request changes nothing */
    NVN_MAP_VIRTUAL_BAD_VIRTUAL /* the virtual range does not fit the virtual pool */
} NvnMapVirtualResult;
/* One checked request, in 64 KiB pages. physical_base is 0 for an unmap and otherwise set by the handler, which checks
   the physical pool's record and range with nvn_pool_range_fits. */
typedef struct {
    uint64_t physical_pool, physical_offset, physical_base, virtual_base, size;
    uint32_t pages;
} NvnMapVirtualRange;
/* Truncates the request to 64 KiB pages, as the library shifts by 16, and checks the virtual range. */
NvnMapVirtualResult nvn_memory_pool_map_virtual_range(const NvnMapVirtualRequest* request, uint64_t virtual_base,
                                                      uint64_t virtual_size, NvnMapVirtualRange* range);
/* The request count of the next batch after done requests; 0 when done has reached count. */
uint32_t nvn_pool_map_virtual_batch(uint32_t count, uint32_t done);

#define NVN_SAMPLER_FILTER_DEFAULT 1u
#define NVN_SAMPLER_WRAP_DEFAULT 1u
#define NVN_SAMPLER_COMPARE_DEFAULT 2u
#define NVN_SAMPLER_MAX_LOD_DEFAULT 1000.0f
#define NVN_SAMPLER_ANISOTROPY_DEFAULT 1.0f
#define NVN_SAMPLER_DESCRIPTOR_WORDS 8u
#define NVN_SAMPLER_DESCRIPTOR_BYTES 32u

typedef struct {
    uint64_t device;
    uint32_t min_filter, mag_filter, wrap[3];
    float min_lod, max_lod, lod_bias;
    uint32_t compare_enable, compare, border[4];
    float anisotropy;
    uint32_t reduction;
    float lod_snap;
} NvnSampler;
typedef struct { uint32_t w1a80, w1a84; } NvnSamplerDeviceWords;
/* Device initialization stores these values (SDK d8312e4 and d8312fc). */
#define NVN_SAMPLER_DEVICE_LOD_BIAS 0u
#define NVN_SAMPLER_DEVICE_FILTER_FLAGS 0x111u
extern const NvnSamplerDeviceWords nvn_sampler_device_words;
void nvn_sampler_defaults(NvnSampler* s);
/* 1 when every field is equal; a NaN float is unequal to itself, as the library fcmp. Pure. */
int nvn_sampler_equal(const NvnSampler* a, const NvnSampler* b);
/* The four border words are raw bits, with no float conversion. */
void nvn_sampler_border(const NvnSampler* s, uint32_t out[4]);
float nvn_sampler_snap_bias(uint32_t index);
/* 0 (out unchanged) when a wrap enum is above 7: the library reads its table unchecked. NaN anisotropy has no defined
   library result. */
int nvn_sampler_descriptor(const NvnSampler* s, const NvnSamplerDeviceWords* dev,
                           uint32_t out[NVN_SAMPLER_DESCRIPTOR_WORDS]);

#define NVN_TEXTURE_FLAG_MINIMAL_TILE 0x2u
#define NVN_TEXTURE_FLAG_COMPRESS 0x8u
#define NVN_TEXTURE_FLAG_LINEAR 0x10u
#define NVN_TEXTURE_FLAG_SPARSE 0x20u
#define NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET 0x40u
#define NVN_TEXTURE_FLAG_ADAPTIVE_TILE 0x200u
/* Bits that only the storage class and ZCull paths read. */
#define NVN_TEXTURE_FLAG_ZCULL_INFO 0x80u         /* bit 7: the device ZCull info path, not implemented */
#define NVN_TEXTURE_FLAG_KIND_MINUS_FIVE 0x100u   /* bit 8: the kind of the 32 row is five lower */
#define NVN_TEXTURE_FLAG_ZCULL_STENCIL 0x400u     /* bit 10: a stencil-only format has a ZCull size */
#define NVN_TEXTURE_FLAG_KIND_S4_PLUS_TWO 0x1000u /* bit 12: the kind of two S 4 rows is two higher */
#define NVN_TEXTURE_POOL_STORAGE_FLAGS 0x280u
#define NVN_TEXTURE_SPARSE_TILE_MASK 0x32u
#define NVN_TEXTURE_KIND_PITCH 0xfeu
#define NVN_TEXTURE_LINEAR_FLAGS (NVN_TEXTURE_FLAG_LINEAR | NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET)
#define NVN_TEXTURE_TARGET_1D 0u
#define NVN_TEXTURE_TARGET_2D 1u
#define NVN_TEXTURE_TARGET_3D 2u
#define NVN_TEXTURE_TARGET_1D_ARRAY 3u
#define NVN_TEXTURE_TARGET_2D_ARRAY 4u
#define NVN_TEXTURE_TARGET_2D_MS 5u
#define NVN_TEXTURE_TARGET_2D_MS_ARRAY 6u
#define NVN_TEXTURE_TARGET_RECTANGLE 7u
#define NVN_TEXTURE_TARGET_CUBE 8u
#define NVN_TEXTURE_TARGET_CUBE_ARRAY 9u
#define NVN_TEXTURE_TARGET_BUFFER 10u
#define NVN_TEXTURE_CUBE_FACES 6u
#define NVN_TEXTURE_FORMAT_MAX 0xa0u
#define NVN_TEXTURE_GOB_WIDTH 64u
#define NVN_TEXTURE_GOB_HEIGHT 8u
#define NVN_TEXTURE_GOB_BYTES 512u
/* Automatic tile choice: the 1.5 times size of the depth (3D) or of the block rows (other targets) selects the tile exponent. */
#define NVN_TEXTURE_AUTO_TILE_DEPTH_4 15u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_3 8u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_2 3u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_1 1u
#define NVN_TEXTURE_AUTO_TILE_ROWS_4 128u
#define NVN_TEXTURE_AUTO_TILE_ROWS_3 64u
#define NVN_TEXTURE_AUTO_TILE_ROWS_2 31u
#define NVN_TEXTURE_AUTO_TILE_ROWS_1 15u
#define NVN_TEXTURE_LINEAR_ALIGNMENT 0x20u
#define NVN_TEXTURE_LINEAR_RT_ALIGNMENT 0x80u
#define NVN_TEXTURE_COMPRESSION_DISABLED 0x76165656u
/* Device initialization values checked against the library disassembly. */
#define NVN_TEXTURE_DEVICE_PAGE_SIZE 0x10000u
#define NVN_TEXTURE_DEVICE_COLOR_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_DEPTH_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_BUFFER_KIND 0u
/* The game's device holds 1 at +0x1b1c (selftest run, 2026-11-03), and the 24 size cases match with it. The store that
   sets it is not found: the value is from one run. */
#define NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE 1u
#define NVN_TEXTURE_DEVICE_DIMENSION_LIMIT 0x4000u
#define NVN_TEXTURE_DEVICE_SECTOR_LAYOUT 2u
#define NVN_TEXTURE_DEVICE_HEADER_ENABLE 1u
#define NVN_TEXTURE_DEVICE_QUALITY 0u
#define NVN_TEXTURE_DEVICE_QUALITY_OVERRIDE 0xffffffffu
#define NVN_TEXTURE_DEVICE_DESCRIPTOR_ROW 2u
/* The GM20B ZCULL words of the device: these must stay equal to the gm20b_zcull_info reply and to the context size
   in src/nx/hle/services/nvdrv.c. */
#define NVN_ZCULL_WIDTH_ALIGN_PIXELS 0x20u
#define NVN_ZCULL_HEIGHT_ALIGN_PIXELS 0x20u
#define NVN_ZCULL_PIXEL_SQUARES_BY_ALIQUOTS 0x400u
#define NVN_ZCULL_ALIQUOT_TOTAL 0x800u
#define NVN_ZCULL_REGION_BYTE_MULTIPLIER 0x20u
#define NVN_ZCULL_REGION_HEADER_SIZE 0x20u
#define NVN_ZCULL_SUBREGION_HEADER_SIZE 0xc0u
#define NVN_ZCULL_SUBREGION_COUNT 0x10u
#define NVN_ZCULL_SUBREGION_WIDTH_ALIGN_PIXELS 0x20u
#define NVN_ZCULL_SUBREGION_HEIGHT_ALIGN_PIXELS 0x40u
#define NVN_ZCULL_CTX_SIZE 1u
#define NVN_ZCULL_CTX_ALIGN 0x1000u

typedef struct {
    uint64_t device, pool, offset, packaged;
    uint32_t flags, target, width, height, depth, levels, format, samples;
    uint32_t swizzle[4], depth_stencil_mode, stride, gl_name;
    uint16_t tile;
} NvnTextureBuilder;
typedef struct {
    uint32_t wac, w19f8, w19fc, w1b18, w1b1c;
    uint32_t w08, w1a48, w1a4c, w1a50, w1a54, w1a88;
} NvnTextureDeviceWords;
extern const NvnTextureDeviceWords nvn_texture_device_words;
/* The device words at +0xb8..+0xe4, in the order of that block. */
typedef struct {
    uint32_t width_align_pixels, height_align_pixels, pixel_squares_by_aliquots, aliquot_total;
    uint32_t region_byte_multiplier, region_header_size, subregion_header_size, subregion_count;
    uint32_t subregion_width_align_pixels, subregion_height_align_pixels, ctx_size, ctx_align;
} NvnZcullDeviceWords;
extern const NvnZcullDeviceWords nvn_zcull_device_words;
typedef struct {
    uint64_t size, alignment, layer_stride;
    uint32_t linear_stride, samples, width, height, depth;
    uint8_t bytes_per_block, log2_bytes, sample_x, sample_y, block_width, block_height;
    uint8_t levels, dim, kind, depth_stencil, tile_x, tile_y, tile_z;
} NvnTextureLayout;
void nvn_texture_builder_defaults(NvnTextureBuilder* b);
uint64_t nvn_texture_storage_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev);
/* Returns 0 for unsupported formats. GL imports have size 0 but retain alignment. Kind stays 0. */
int nvn_texture_layout(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, NvnTextureLayout* out);
/* The sparse tile layout of d822538: level count, tile width, tile height, tile depth. 0 (out unchanged) for an
   invalid format. A zero level count is the library result, not a refusal. */
int nvn_texture_sparse_layout(const NvnTextureBuilder* b, uint32_t out[4]);

#define NVN_TEXTURE_DESCRIPTOR_WORDS 8u
#define NVN_TEXTURE_DESCRIPTOR_BYTES 32u
#define NVN_VIEW_LEVELS 0x1u
#define NVN_VIEW_LAYERS 0x2u
#define NVN_VIEW_FORMAT 0x4u
#define NVN_VIEW_SWIZZLE 0x8u
#define NVN_VIEW_DEPTH_STENCIL_MODE 0x10u
#define NVN_VIEW_TARGET 0x20u
#define NVN_VIEW_COMPARE_MASK 0x3Fu /* the library compares only these flag bits */

typedef struct {
    uint8_t flags, base_level, level_count;
    uint16_t base_layer, layer_count;
    uint32_t format, swizzle[4], depth_stencil_mode, target;
} NvnTextureView;
typedef struct {
    NvnTextureBuilder builder;
    NvnTextureLayout layout;
    uint64_t address;
} NvnTexture;
/* The getters of the builder and object records. The packaged layout word is the inverse of the setter; the raw
   storage class comes from the flags and the format only. */
#define NVN_TEXTURE_RAW_STORAGE_FORMAT_LAST 0x36u
uint32_t nvn_texture_packaged_layout(uint16_t tile, uint32_t* layout);
uint32_t nvn_texture_raw_storage_class(uint32_t flags, uint32_t format);
uint32_t nvn_texture_storage_size(const NvnTexture* t);
/* The library result 0x100 | kind. An invalid format record gives kind 0, which is not a refusal. */
#define NVN_TEXTURE_STORAGE_CLASS_BASE 0x100u
uint32_t nvn_texture_storage_class(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev);
/* 1 fills the size, 0 leaves it 0 for a format without a ZCull surface, and a refusal means flags bit 7 or a zero
   device divisor. The library returns a u32; the host returns its low 32 bits. */
#define NVN_TEXTURE_ZCULL_REFUSAL (-1)
int nvn_texture_zcull_size(const NvnTextureBuilder* b, const NvnZcullDeviceWords* zdev, uint32_t* size);
/* The fields that nvnTextureCompare compares (sdk d83e3a0). The builder packaged data, tile, stride and GL name are
   inputs and stay out, and so does the layout alignment. Pure. */
int nvn_texture_equal(const NvnTexture* a, const NvnTexture* b);
/* The optional fields of a view are compared only when the same flag bit is set in both views (sdk d83c9a0). Pure. */
int nvn_texture_view_equal(const NvnTextureView* a, const NvnTextureView* b);
/* The view offset of d83d01c: the level offset plus the layer offset. 0 (out unchanged) when a block linear
   base level reaches 32, which wraps the library shift count. Linear and BUFFER textures give 0. */
int nvn_texture_view_offset(const NvnTexture* t, const NvnTextureView* v, uint64_t* out);
/* One mip level of a block linear texture: the offset and bytes of the level in one layer, the block columns (GOBs
   across), the block rows, the level depth, the trimmed block height exponent (2^bh GOBs high) and the trimmed
   depth exponent (2^bd slices deep). */
typedef struct {
    uint64_t offset, size;
    uint32_t gobs_x, rows, depth;
    uint8_t block_height_log2, depth_log2;
} NvnTextureLevel;
/* 0 (out unchanged) for a linear texture, the BUFFER target, a multisample texture or level too large. */
int nvn_texture_level(const NvnTexture* t, uint32_t level, NvnTextureLevel* out);
/* Copies the bytes [x_start, x_end) of the source rows [row_start, row_end) of slice z into the block linear bytes
   of one level (z = 0 for a 2D level). 0 with no write when z is outside the level depth, the region leaves the level
   or an offset would pass the size of the level. */
int nvn_texture_write_rows(uint8_t* level_base, const NvnTextureLevel* lv, uint32_t x_start, uint32_t x_end,
                           uint32_t row_start, uint32_t row_end, const uint8_t* src, uint64_t src_stride,
                           uint32_t z);
/* The reverse of nvn_texture_write_rows: the bytes [x_start, x_end) of the block linear rows [row_start, row_end) of
   slice z of one level go to the linear rows of dst at dst_stride. 0 with no write when z is outside the level depth,
   the region leaves the level or an offset would pass the size of the level. */
int nvn_texture_read_rows(const uint8_t* level_base, const NvnTextureLevel* lv, uint32_t x_start, uint32_t x_end,
                          uint32_t row_start, uint32_t row_end, uint8_t* dst, uint64_t dst_stride, uint32_t z);
/* seed is the u32 at byte offset 0x24, not u32 array index 0x24 (d858d28). */
typedef struct {
    uint32_t seed;
    uint8_t depth_size, stencil_size, one_code, srgb;
} NvnTextureFormatRecord;
typedef enum {
    NVN_TEXTURE_DESCRIPTOR_OK = 0,
    NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT,
    NVN_TEXTURE_DESCRIPTOR_BAD_TARGET,
    NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE
} NvnTextureDescriptorResult;
typedef enum {
    NVN_TEXTURE_DESCRIPTOR_MODE_TEXTURE = 0,
    NVN_TEXTURE_DESCRIPTOR_MODE_IMAGE = 1
} NvnTextureDescriptorMode;
#define NVN_TEXTURE_SWIZZLE_MAX 5u /* ZERO, ONE, R, G, B, A */
uint16_t nvn_texture_packaged_tile(uint32_t layout);
/* The caller supplies the effective format record. Errors leave out unchanged. */
NvnTextureDescriptorResult nvn_texture_descriptor(const NvnTexture* texture, const NvnTextureView* view,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* format, NvnTextureDescriptorMode mode,
    uint32_t out[NVN_TEXTURE_DESCRIPTOR_WORDS]);

/* Returns the Maxwell RT format word of an NVN format, or 0 when the format has none or is above NVN_TEXTURE_FORMAT_MAX.
   A 0 result is a fault of the caller: log it and stop. */
uint32_t nvn_rt_format_word(uint32_t format);

/* The Android buffer format and libnx NvLayout values of the window image show (3c decision 9). */
#define NVN_PRESENT_FORMAT_RGBA8888 1u
#define NVN_PRESENT_LAYOUT_PITCH    1u
#define NVN_PRESENT_LAYOUT_BLOCK    3u
typedef struct {
    uint64_t cpu_addr;
    uint32_t w, h, pitch, layout, bhl, fmt;
    int32_t crop[4];
} NvnPresentArgs;
/* The arguments of present_buffer for the window image in texture t, its storage in pool_storage. 0 with out
   unchanged when the format is not RGBA8, the address sum overflows or a crop value does not fit int32_t. Pure. */
int nvn_window_present_args(const NvnTexture* t, uint64_t pool_storage, const NvnWindowCrop* crop, NvnPresentArgs* out);
/* The refusal reason of nvnQueuePresentTexture, in the order of the checks. It reads states only after the index
   passed, so states may be NULL with an index outside the window. NULL when the present may go on. Pure. */
const char* nvn_present_refusal(int queue_ok, int window_ok, int32_t index, uint32_t texture_count,
                                const uint8_t* states);
/* nvnEventSignal: mode 0 writes the value, mode 1 adds it to the old word. Any other mode writes, as in the library.
   The add wraps at 32 bits. Pure. */
#define NVN_EVENT_SIGNAL_WRITE 0u
#define NVN_EVENT_SIGNAL_ADD   1u
uint32_t nvn_event_signal_value(uint32_t mode, uint32_t old, uint32_t value);
/* nvnCommandBufferWaitEvent: mode 1 is GEQUAL with a 32-bit wrap, any other mode is EQUAL, as in the library. Pure. */
#define NVN_EVENT_WAIT_EQUAL       0u
#define NVN_EVENT_WAIT_GEQUAL_WRAP 1u
int nvn_event_wait_met(uint32_t mode, uint32_t word, uint32_t value);

/* The pipeline stages of a program: 0 vertex, 1 fragment (the only two the game uses), 2 to 5 accepted by the library. */
#define NVN_PROGRAM_STAGES 6u
#define NVN_PROGRAM_STAGE_COMPUTE 5u  /* compute: its record holds plain data, not method words */
#define NVN_PROGRAM_STAGE_COUNT 6u    /* the bound program array of a frame job, bit t is stage t (bit 5 compute) */
#define NVN_PROGRAM_STAGE_MASK_ALL 0x3Fu /* bits 0 to 5: the only stage bits that the library accepts */
/* Header +8 is the layout version (5 to 16), not the stage. The stage index is the u32 at header +0x714 (sdk d837228). */
#define NVN_PROGRAM_LAYOUT_FIRST 5u
#define NVN_PROGRAM_LAYOUT_LAST 16u
#define NVN_PROGRAM_STAGE_OFFSET 0x714u
/* The words of a shader header at +4, +0xc and +0x10 that the library checks. */
#define NVN_PROGRAM_HEADER_VERSION 1u
#define NVN_PROGRAM_HEADER_SIZE 0x120u
#define NVN_PROGRAM_HEADER_KIND 0xbu
/* 1 and *index_out = stage_word for a valid header; 0 (index_out unchanged) for any other word or a NULL index_out. */
int nvn_program_header_stage(uint32_t version, uint32_t layout, uint32_t size_word, uint32_t kind_word,
                             uint32_t stage_word, uint32_t* index_out);

/* The stages that the host builder builds (see below) and the refusal texts of one entry. */
#define NVN_PROGRAM_STAGE_VERTEX 0u
#define NVN_PROGRAM_STAGE_FRAGMENT 1u
#define NVN_PROGRAM_STAGE_SUPPORTED_MASK 0x23u /* vertex, fragment and compute */
#define NVN_PROGRAM_HEADER_ALIGNMENT 8u
#define NVN_PROGRAM_REFUSAL_WORD_ZERO "entry word is zero"
#define NVN_PROGRAM_REFUSAL_HEADER_NULL "header is null"
#define NVN_PROGRAM_REFUSAL_HEADER_UNALIGNED "header is not aligned"

/* The check of the count and the entries pointer of a SetShaders call, in the order of the checks. */
typedef enum {
    NVN_PROGRAM_COUNT_OK = 0,
    NVN_PROGRAM_COUNT_NEGATIVE,
    NVN_PROGRAM_COUNT_ABOVE,
    NVN_PROGRAM_COUNT_ENTRIES_NULL
} NvnProgramCountCheck;
NvnProgramCountCheck nvn_program_count_check(int32_t count, uint64_t entries);
/* The refusal of one entry before its header is read: the word of an Ext entry, the header pointer, its alignment. */
const char* nvn_program_entry_refusal(int has_control, uint64_t entry_word, uint64_t header);
/* 1 when the stage bit was clear and is set now, 0 when the stage is already in the mask. */
int nvn_program_stage_add(uint32_t* stage_mask, uint32_t stage_index);
/* The lowest stage above fragment that the mask holds and the host does not build, or -1. */
int nvn_program_unsupported_stage(uint32_t stage_mask);

/* The Maxwell command words that nvnProgramSetShadersExt builds for the shaders of a program. The host builds them
   because it has no library. Stage 0 (vertex, no VertexA), stage 1 (fragment), stage 5 (compute) and the transform
   feedback block of the vertex stage are built. Stages 2 to 4 are not ported. A compute stage cannot share a program
   with vertex or fragment: the library crashes for that case. */
#define NVN_PROGRAM_XFB_BLOB_BYTES 0x612u      /* the transform feedback blob that follows the vertex header */
#define NVN_PROGRAM_XFB_HEADER_OFFSET 0xDCu    /* the blob starts at the vertex header plus this offset */
#define NVN_PROGRAM_XFB_STREAMS 4u
#define NVN_PROGRAM_XFB_LAYOUT_WORDS_MAX 32u   /* the method range 0xA00+0x20i holds 32 words */
#define NVN_PROGRAM_XFB_WORDS_MAX (NVN_PROGRAM_XFB_STREAMS * (4u + 1u + NVN_PROGRAM_XFB_LAYOUT_WORDS_MAX))
#define NVN_PROGRAM_WORDS_MAX (30u + NVN_PROGRAM_XFB_WORDS_MAX) /* graphics 30, then the transform feedback block */
#define NVN_PROGRAM_VERTEX_WORDS 8u
#define NVN_PROGRAM_FRAGMENT_WORDS 17u
#define NVN_PROGRAM_COMPUTE_WORDS 18u

/* The words that nvnCommandBufferBindProgram keeps for a program: a compute program copies only its 18 words, any
   other program copies the whole array. Returns the count that the op holds. Pure. */
uint32_t nvn_bind_program_words(uint32_t program_mask, const uint32_t* program_words, uint32_t program_word_count,
                                uint32_t* out_words);

/* The flag bits of the compute word 16: bit n is (byte at h+0x830+n) & 1, only for layout 13 and above. */
#define NVN_PROGRAM_COMPUTE_FLAG_0 0x1u
#define NVN_PROGRAM_COMPUTE_FLAG_1 0x2u
#define NVN_PROGRAM_COMPUTE_FLAG_2 0x4u

typedef struct {
    uint32_t code_offset;  /* h+0x708 + d, u32 wrap */
    uint32_t cb_offset;    /* h+0x700 + d, u32 wrap */
    uint32_t header_value; /* h+0x70c */
} NvnProgramStageIn;

/* The stage 5 (compute) words. The builder copies them unchanged: they are raw offsets, not region addresses. */
typedef struct {
    uint32_t code_offset;  /* word 0: h+0x708 + d, u32 wrap */
    uint32_t cb_offset;    /* word 1: h+0x700 + d, u32 wrap */
    uint32_t h6fc;         /* word 2: h+0x6fc */
    uint32_t h710;         /* word 3: h+0x710 */
    uint32_t h70c;         /* word 4: h+0x70c */
    uint32_t block_x;      /* word 5: h+0x748 */
    uint32_t block_y;      /* word 6: h+0x74c */
    uint32_t block_z;      /* word 7: h+0x750 */
    uint32_t shared_bytes; /* word 8: h+0x754 */
    uint32_t local_pos;    /* word 9: h+0x758 */
    uint32_t local_neg;    /* word 10: h+0x75c */
    uint32_t local_crs;    /* word 11: h+0x760 */
    uint32_t barriers;     /* word 12: h+0x764 */
    uint32_t h6f8;         /* word 13: h+0x6f8 */
    uint32_t h778_low;     /* word 14: low u32 of h+0x778 */
    uint32_t h778_high;    /* word 15: high u32 of h+0x778 */
    uint32_t flags;        /* word 16: bits 0 to 2, see the flag constants */
} NvnProgramComputeIn;

/* The stage 5 (compute) dispatch inputs and outputs that the host builds: the launch description (QMD) and the
   driver constant block that the library submit path fills itself. */
#define NVN_COMPUTE_QMD_WORDS 64u
#define NVN_COMPUTE_CBUF0_BYTES 0x500u
#define NVN_COMPUTE_CBUF0_WORDS (NVN_COMPUTE_CBUF0_BYTES / 4u)
#define NVN_COMPUTE_UNIFORM_SLOTS 32u  /* nvn_cmdlist.h's NVN_UNIFORM_SLOT_MAX */
#define NVN_COMPUTE_STORAGE_SLOTS 128u /* nvn_cmdlist.h's NVN_STORAGE_SLOT_MAX */
#define NVN_COMPUTE_DESCRIPTOR_SLOTS 16u /* each table of the driver block: 0x100 bytes of 16 byte entries */
#define NVN_COMPUTE_UNIFORM_QBUF_SLOTS 5u /* stage 5 uniform slots 0 to 4 map to QMD constant buffers 3 to 7 */
#define NVN_COMPUTE_CBUF0_UNIFORM_OFFSET 0x210u
#define NVN_COMPUTE_CBUF0_STORAGE_OFFSET 0x310u
#define NVN_COMPUTE_DESCRIPTOR_BYTES 16u
#define NVN_COMPUTE_CBUF_ADDRESS_HIGH_SHIFT 15u /* the size field of a QMD constant buffer config word */
/* nvn_native.c's NVN_SHADER_REGION_BASE: the window that the program offsets are relative to. */
#define NVN_COMPUTE_SHADER_REGION_BASE NVN_POOL_GPU_BASE

typedef struct {
    uint64_t addr;
    uint64_t size;
    uint8_t bound;
} NvnComputeBindEntry;

/* One compute dispatch: the scratch block that becomes constant buffer 0, the stage 5 uniform slots and the storage
   slots. A slot without bound is empty. */
typedef struct {
    uint64_t cbuf0_address;
    NvnComputeBindEntry uniform[NVN_COMPUTE_UNIFORM_SLOTS];
    NvnComputeBindEntry storage[NVN_COMPUTE_STORAGE_SLOTS];
} NvnComputeBinds;

/* Builds the 64-word launch description. Returns 0 on a refusal and leaves qmd unchanged. QMD sizes must fit in 17
   bits after padding. Uniform slots 0 to 4 become QMD constant buffers 3 to 7, the others use 32-bit driver sizes. Pure. */
int nvn_compute_qmd_build(const NvnProgramComputeIn* prog, uint32_t x, uint32_t y, uint32_t z,
                          const NvnComputeBinds* binds, uint32_t qmd[NVN_COMPUTE_QMD_WORDS]);
/* Why nvn_compute_qmd_build refuses. NVN_QMD_OK means that it builds. */
enum {
    NVN_QMD_OK = 0,
    NVN_QMD_NULL_ARGUMENT,
    NVN_QMD_GRID_X,
    NVN_QMD_GRID_Y,
    NVN_QMD_GRID_Z,
    NVN_QMD_CBUF1_SIZE,
    NVN_QMD_UNIFORM_SIZE, /* a bound uniform slot with a size above 32 bits */
    NVN_QMD_UNIFORM_QBUF_SIZE /* a rounded uniform size above the QMD field */
};
/* The refusal check of nvn_compute_qmd_build. *detail gets the slot for either uniform size refusal, else 0. Pure. */
uint32_t nvn_compute_qmd_check(const NvnProgramComputeIn* prog, uint32_t x, uint32_t y, uint32_t z,
                               const NvnComputeBinds* binds, uint32_t* detail);
const char* nvn_compute_qmd_reason_name(uint32_t reason);
/* Builds the 0x500-byte driver constant block with the uniform and storage descriptor tables. Returns 0 on a refusal
   (a bound slot of 16 or more, a size above 32 bits, or a null argument). Pure. */
int nvn_compute_cbuf0_build(const NvnComputeBindEntry* uniform, const NvnComputeBindEntry* storage,
                            uint32_t words[NVN_COMPUTE_CBUF0_WORDS]);

typedef struct {
    uint8_t present;
    uint8_t g148, g150, g158, g160, g168, g170, g178, g180, g188;
    uint32_t g190, g198;
    uint32_t g1a0[4];
} NvnProgramFragmentIn;

typedef struct {
    uint32_t d1a38, d1a3c, d1a40, d1a68, d1a70, d1a74, d94, da4;
} NvnProgramDeviceIn;

/* Builds the word array of the stages that are present (NULL is absent). A compute block comes first, then vertex,
   then fragment. program_id is the id word of the bind macros: the device's counter after its increment, never 0
   for a vertex or fragment stage. Returns 0 on a refusal and leaves *count unchanged; on success sets *count and
   writes the words. Pure. */
int nvn_program_build_words(const NvnProgramComputeIn* compute, const NvnProgramStageIn* vertex,
                            const NvnProgramStageIn* fragment, const NvnProgramFragmentIn* frag,
                            const NvnProgramDeviceIn* dev, uint32_t program_id, uint64_t region_base, uint32_t* words,
                            uint32_t* count);
/* Reverses program_build_compute: fills *out from an 18-word compute block. Returns 0 when count is not 18 or an
   argument is null. Pure. */
int nvn_program_compute_from_words(const uint32_t* words, uint32_t count, NvnProgramComputeIn* out);
/* The header bytes that a compute stage reads, by layout: 0x833 for 13 to 16, 0x780 for 5 to 12, else 0. */
uint32_t nvn_program_compute_header_bytes(uint32_t layout);
/* Reads the compute words from a host copy of the header at h. Returns 0 (out unchanged) on a null argument, a
   layout outside 5 to 16, or a byte count below nvn_program_compute_header_bytes(layout). Pure. */
int nvn_program_compute_in_from_header(const uint8_t* header, uint32_t bytes, uint32_t entry_word,
                                       NvnProgramComputeIn* out);

/* Builds the transform feedback block that the library appends after the graphics words, from the 0x612-byte blob
   at vertex header + 0xDC. Returns 1 with *count = 0 when the blob is disabled, and 1 with the words when it is
   good. Returns 0 with a static *reason on a refusal: a comps above 0x80, an id outside the table, a layout above
   32 words or above the stride allocation, or a word array that is too small. Writes at most max words. Pure. */
int nvn_program_xfb_words(const uint8_t* blob, uint32_t* words, uint32_t max, uint32_t* count, const char** reason);

/* The pushbuffer stream of the method words of a program (gpu.c's pb_decode). Mode 1 increments the method for each
   data word, mode 5 sends the first word to the header method and the rest to the next one, and mode 4 carries its value in the count field. A macro call gets last only on its
   last data word. Pure: the struct holds the state between the words. */
typedef struct { uint32_t mode, method, remaining, subchannel; } NvnWordStream;
/* 1 for a method call (*method, *value, *last set, s->subchannel is the subchannel of its header, bits 15:13), 0 for a
   header that starts a run, -1 for a refusal. */
int nvn_word_stream_next(NvnWordStream* s, uint32_t word, uint32_t* method, uint32_t* value, int* last);
/* 1 when no run is left open (remaining 0), so the array ended on a method call. */
int nvn_word_stream_complete(const NvnWordStream* s);

/* The words that nvnCommandBufferBarrier writes, in order (nvn-design.md, decision 6, item "Barrier"). The bit 3
   range records are not words: they split the library's command memory, not this stream. Bits 9 to 31 are ignored. */
#define NVN_BARRIER_WORDS_MAX 14u
/* Fills words with at most cap words and returns the count. Returns 0 and writes nothing when words is NULL or cap is
   too small. device_byte is the device byte at +0x60. Pure. */
uint32_t nvn_barrier_words(uint32_t bits, uint32_t device_byte, uint32_t* words, uint32_t cap);

/* The pushbuffer stream of a command image (the library's macro code of a queue). Calls callback for each method
   word. Only subchannel 0 is accepted. Returns 0 when the words are exactly used up, or -1 with the index of the
   refused word in *error_index. Type 0 words are no-ops. Pure. */
typedef void (*NvnPushbufMethod)(void* ctx, uint32_t method, uint32_t value);
int nvn_pushbuf_decode(const uint32_t* words, uint32_t count, NvnPushbufMethod callback, void* ctx,
                       uint32_t* error_index);

/* The method and value pairs that the library writes for each requested stage that the program does not have, in the
   order 3, 4, 2, 1. The stage is disabled by method 0x820 bit 0 = 0, so the 0x821 offset is not used. */
#define NVN_PROGRAM_DISABLE_MAX 7u
#define NVN_PROGRAM_DISABLE_TCS_OFFSET 0u
typedef struct { uint32_t method, value; } NvnProgramMethod;
/* Fills out and returns the count (0 to NVN_PROGRAM_DISABLE_MAX). Pure. */
uint32_t nvn_program_disable_methods(uint32_t requested_mask, uint32_t program_mask,
                                     NvnProgramMethod out[NVN_PROGRAM_DISABLE_MAX]);

/* 1 when [at, at+size) is inside [lo, hi). The comparison cannot overflow. A size of 0 is inside when lo <= at <= hi.
   Pure. */
int nvn_span_inside(uint64_t at, uint64_t size, uint64_t lo, uint64_t hi);
/* The index of a name of the NVN library's sorted list (the names without the "nvn" prefix), or -1 when the name is
   not in the list or does not start with "nvn". The list has no empty name (nvn_proc_table_check). Pure. */
int nvn_proc_name_index(const char* const* names, uint32_t count, const char* name);
/* -1 when the names and their address table are good, else the index of the first bad entry: an empty or unsorted name
   (a duplicate included), or an address outside [code_lo, code_hi) or not a multiple of 4. Pure. */
int nvn_proc_table_check(const char* const* names, const uint64_t* addrs, uint32_t count, uint64_t code_lo,
                         uint64_t code_hi);

#endif
