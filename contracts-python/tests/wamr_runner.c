#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_export.h"

static uint8_t *read_file(const char *path, uint32_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || file_size > UINT32_MAX) {
        fclose(file);
        return NULL;
    }
    *size = (uint32_t)file_size;
    rewind(file);
    uint8_t *result = malloc(*size);
    if (result == NULL || fread(result, 1, *size, file) != *size) {
        free(result);
        fclose(file);
        return NULL;
    }
    fclose(file);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        return 64;
    }
    uint32_t        module_size       = 0;
    uint32_t        input_size        = 0;
    uint8_t        *module_bytes      = read_file(argv[1], &module_size);
    uint8_t        *input             = read_file(argv[2], &input_size);
    char            error[256]        = { 0 };
    RuntimeInitArgs runtime_arguments = { 0 };
    runtime_arguments.mem_alloc_type  = Alloc_With_System_Allocator;
    if (module_bytes == NULL || input == NULL || !wasm_runtime_full_init(&runtime_arguments)) {
        return 65;
    }
    wasm_module_t module = wasm_runtime_load(module_bytes, module_size, error, sizeof(error));
    if (module == NULL) {
        fprintf(stderr, "%s\n", error);
        return 66;
    }
    InstantiationArgs instance_arguments = {
        .default_stack_size     = 256 * 1024,
        .host_managed_heap_size = 1024 * 1024,
        .max_memory_pages       = 128,
    };
    wasm_module_inst_t instance = wasm_runtime_instantiate_ex(module, &instance_arguments, error, sizeof(error));
    if (instance == NULL) {
        fprintf(stderr, "%s\n", error);
        return 67;
    }
    wasm_exec_env_t      environment = wasm_runtime_create_exec_env(instance, 256 * 1024);
    wasm_function_inst_t initialize  = wasm_runtime_lookup_function(instance, "_initialize");
    if (environment == NULL || initialize == NULL
        || !wasm_runtime_call_wasm_a(environment, initialize, 0, NULL, 0, NULL)) {
        fprintf(stderr, "%s\n", wasm_runtime_get_exception(instance));
        return 68;
    }
    void    *native_input = NULL;
    uint64_t input_offset = wasm_runtime_module_malloc(instance, input_size, &native_input);
    if (input_offset == 0 || native_input == NULL) {
        return 69;
    }
    memcpy(native_input, input, input_size);
    wasm_runtime_set_instruction_count_limit(environment, atoi(argv[4]));
    wasm_function_inst_t invoke              = wasm_runtime_lookup_function(instance, "exc_invoke");
    wasm_val_t           invoke_arguments[2] = {
        { .kind = WASM_I32, .of.i32 = (int32_t)input_offset },
        { .kind = WASM_I32, .of.i32 = (int32_t)input_size },
    };
    wasm_val_t output_offset = { .kind = WASM_I32 };
    if (!wasm_runtime_call_wasm_a(environment, invoke, 1, &output_offset, 2, invoke_arguments)) {
        fprintf(stderr, "%s\n", wasm_runtime_get_exception(instance));
        return 70;
    }
    wasm_function_inst_t result_length = wasm_runtime_lookup_function(instance, "exc_result_len");
    wasm_val_t           output_size   = { .kind = WASM_I32 };
    if (!wasm_runtime_call_wasm_a(environment, result_length, 1, &output_size, 0, NULL)) {
        return 71;
    }
    uint32_t result_size   = (uint32_t)output_size.of.i32;
    uint32_t result_offset = (uint32_t)output_offset.of.i32;
    if (result_size != 0 && !wasm_runtime_validate_app_addr(instance, result_offset, result_size)) {
        return 72;
    }
    void *output = result_size == 0 ? NULL : wasm_runtime_addr_app_to_native(instance, result_offset);
    FILE *result = fopen(argv[3], "wb");
    if (result == NULL) {
        return 73;
    }
    if (result_size != 0 && fwrite(output, 1, result_size, result) != result_size) {
        fclose(result);
        return 74;
    }
    fclose(result);
    return 0;
}
