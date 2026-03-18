#include "rive_thorvg.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_W 256
#define TEST_H 256
#define TEST_FRAMES 60

static uint8_t* load_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)len);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (n != (size_t)len) {
        free(data);
        return NULL;
    }

    *out_len = (size_t)len;
    return data;
}

static uint64_t fnv1a64(const uint8_t* data, size_t len) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int main(int argc, char** argv) {
    const char* riv_path = (argc > 1) ? argv[1] : "assets/loader.riv";

    size_t riv_len = 0;
    uint8_t* riv_data = load_file(riv_path, &riv_len);
    if (!riv_data) {
        fprintf(stderr, "smoke: failed to load %s\n", riv_path);
        return 2;
    }

    size_t pixel_bytes = (size_t)TEST_W * (size_t)TEST_H * sizeof(uint32_t);
    uint32_t* buffer = (uint32_t*)calloc((size_t)TEST_W * (size_t)TEST_H, sizeof(uint32_t));
    if (!buffer) {
        fprintf(stderr, "smoke: framebuffer alloc failed\n");
        free(riv_data);
        return 3;
    }

    rive_thorvg_init(1);

    int exit_code = 0;
    rive_instance_t* rive = rive_create(buffer, TEST_W, TEST_H, RIVE_PIXEL_FORMAT_ARGB8888);
    if (!rive) {
        fprintf(stderr, "smoke: rive_create failed\n");
        exit_code = 4;
        goto cleanup;
    }

    rive_set_fit(rive, RIVE_FIT_CONTAIN, RIVE_ALIGN_CENTER);

    if (!rive_load(rive, riv_data, riv_len)) {
        fprintf(stderr, "smoke: rive_load failed\n");
        exit_code = 5;
        goto cleanup;
    }

    bool changed = rive_render(rive);
    uint64_t first_hash = fnv1a64((const uint8_t*)buffer, pixel_bytes);
    uint64_t prev_hash = first_hash;
    uint64_t last_hash = first_hash;

    int changed_frames = changed ? 1 : 0;
    int hash_changes = 0;

    for (int i = 0; i < TEST_FRAMES; ++i) {
        if (rive_advance(rive, 1.0f / 60.0f)) {
            changed_frames++;
        }

        last_hash = fnv1a64((const uint8_t*)buffer, pixel_bytes);
        if (last_hash != prev_hash) {
            hash_changes++;
            prev_hash = last_hash;
        }
    }

    printf("smoke: first=0x%016llx last=0x%016llx changed_frames=%d hash_changes=%d\n",
           (unsigned long long)first_hash,
           (unsigned long long)last_hash,
           changed_frames,
           hash_changes);

    if (first_hash == 0ULL || last_hash == 0ULL || changed_frames <= 0 || hash_changes <= 0) {
        fprintf(stderr, "smoke: failed invariants\n");
        exit_code = 6;
    }

cleanup:
    if (rive) rive_destroy(rive);
    rive_thorvg_term();
    free(buffer);
    free(riv_data);
    return exit_code;
}
