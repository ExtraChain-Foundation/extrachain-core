#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "py/gc.h"
#include "py/obj.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/stackctrl.h"

enum {
    EXCO_GC_HEAP_BYTES = 4 * 1024 * 1024,
    EXCO_RESULT_BYTES = 256 * 1024,
};

static uint8_t gc_heap[EXCO_GC_HEAP_BYTES];
static uint8_t result[EXCO_RESULT_BYTES];
static uint32_t result_length;

static uint32_t read_u32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) | ((uint32_t)source[2] << 8)
           | source[3];
}

static mp_obj_t load_mpy(const uint8_t *source, size_t length) {
    mp_module_context_t *context = m_new_obj(mp_module_context_t);
    context->module.globals = mp_globals_get();
    mp_compiled_module_t module;
    module.context = context;
    mp_raw_code_load_mem(source, length, &module);
    mp_obj_t function = mp_make_function_from_proto_fun(module.rc, context, MP_OBJ_NULL);
    return mp_call_function_0(function);
}

__attribute__((export_name("exc_invoke"))) uint32_t exc_invoke(const uint8_t *input, uint32_t length) {
    result_length = 0;
    if (length < 4) {
        return 0;
    }

    const uint32_t bytecode_length = read_u32(input);
    if (bytecode_length > length - 4) {
        return 0;
    }

    int stack_top;
    mp_stack_set_top(&stack_top);
    gc_init(gc_heap, gc_heap + sizeof(gc_heap));
    mp_init();

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        load_mpy(input + 4, bytecode_length);
        mp_obj_t dispatch = mp_load_global(qstr_from_str("__exc_dispatch"));
        mp_obj_t request = mp_obj_new_bytes(input + 4 + bytecode_length, length - 4 - bytecode_length);
        mp_obj_t output = mp_call_function_1(dispatch, request);
        mp_buffer_info_t buffer;
        mp_get_buffer_raise(output, &buffer, MP_BUFFER_READ);
        if (buffer.len <= sizeof(result)) {
            memcpy(result, buffer.buf, buffer.len);
            result_length = buffer.len;
        }
        nlr_pop();
    }

    mp_deinit();
    return (uint32_t)(uintptr_t)result;
}

__attribute__((export_name("exc_result_len"))) uint32_t exc_result_len(void) {
    return result_length;
}

void gc_collect(void) {
}

void nlr_jump_fail(void *value) {
    (void)value;
    __builtin_trap();
}

void mp_hal_stdout_tx_strn(const char *source, size_t length) {
    (void)source;
    (void)length;
}

void mp_hal_stdout_tx_strn_cooked(const char *source, size_t length) {
    (void)source;
    (void)length;
}
