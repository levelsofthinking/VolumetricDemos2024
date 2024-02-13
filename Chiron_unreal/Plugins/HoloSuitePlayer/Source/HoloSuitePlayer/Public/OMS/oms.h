// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#ifndef OMS_H
#define OMS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "HoloMeshSkeleton.h"

#define OMS_VERSION 10
#define OMS_BAD_VERSION -1
#define OMS_READ_ERROR -2

const uint8_t kOMSKeyframePositionMask = 0x01;
const uint8_t kOMSKeyframeRotationMask = 0x02;

typedef union oms_vec2_t {
    struct
    {
        float x;
        float y;
    };

    float data[2];
} oms_vec2_t;

typedef union oms_vec3_t {
    struct
    {
        float x;
        float y;
        float z;
    };

    float data[3];
} oms_vec3_t;

typedef union oms_vec4_t {
    struct
    {
        float x;
        float y;
        float z;
        float w;
    };

    float data[4];
} oms_vec4_t;

typedef union oms_quaternion_t {
    struct
    {
        float x;
        float y;
        float z;
        float w;
    };

    float data[4];
} oms_quaternion_t;

typedef struct oms_matrix4x4_t {
    float m[16];
} oms_matrix4x4_t;

typedef struct oms_retarget_data_t {
    int bone_count;
    oms_vec4_t* weights;
    oms_vec4_t* indices;
    char** bone_names;
    int* bone_parents;

    uint8_t** keyframes;
    oms_vec3_t** bone_positions;
    oms_quaternion_t** bone_rotations;

} oms_retarget_data_t;

typedef struct oms_aabb_t {
    oms_vec3_t min;
    oms_vec3_t max;
} oms_aabb_t;

typedef struct oms_ssdr_frame_t {
    oms_matrix4x4_t* matrices;
} oms_ssdr_frame_t;

typedef struct oms_delta_frame_t {
    oms_vec3_t* vertices;
} oms_delta_frame_t;

typedef enum oms_compression_type {
    OMS_COMPRESSION_NONE = 0,
    OMS_COMPRESSION_GZIP = 1,
    OMS_COMPRESSION_ZSTD = 2,
    OMS_COMPRESSION_DELTA = 3
} oms_compression_type;

typedef struct sequence_table_entry {
    uint32_t frame_count;
    uint32_t start_frame;
    uint32_t end_frame;
    uint64_t start_byte;
    uint64_t end_byte;
} sequence_table_entry;

typedef struct oms_header_t {
    int version;
    int sequence_count;
    bool has_retarget_data;
    uint8_t compression_level;

    uint32_t frame_count;
    sequence_table_entry* sequence_table_entries;
} oms_header_t;

typedef struct oms_sequence_extras_t {
    int* ssdr_weights_packed;
} oms_sequence_extras_t;

typedef struct oms_sequence_t {
    oms_aabb_t aabb;
    int vertex_count;
    oms_vec3_t* vertices;

    int normal_count;
    oms_vec3_t* normals;

    int uv_count;
    oms_vec2_t* uvs;

    int index_count;
    void* indices;

    oms_vec4_t* ssdr_bone_indices;
    oms_vec4_t* ssdr_bone_weights;

    int ssdr_frame_count;
    int ssdr_bone_count;
    oms_ssdr_frame_t* ssdr_frames;

    int delta_frame_count;
    oms_delta_frame_t* delta_frames;

    oms_retarget_data_t retarget_data;

    oms_sequence_extras_t extras;

} oms_sequence_t;

typedef struct oms_write_sequences_options_t {
    bool use_packed_ssdr_weights;
    bool anim_keyframe_compression;
} oms_write_sequences_options_t;

#ifdef _WIN32
#define LIB_OMS_DLLFLAGS __declspec(dllexport)
#else
#define LIB_OMS_DLLFLAGS 
#endif

// Inhibit C++ name mangling for liboms functions.
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

    // Reads an oms_header_t from the buffer and returns number of bytes read.
    // Returns OMS_BAD_VERSION if the version of the file does not match OMS_VERSION
    LIB_OMS_DLLFLAGS size_t oms_read_header(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_out);

    // Parses the bytes in a buffer until the OMS data is found (this is used for when OMS data is packaged in an mp4)
    // Reads an oms_header_t from the buffer and returns number of bytes read.
    // Returns OMS_BAD_VERSION if the version of the file does not match OMS_VERSION
    LIB_OMS_DLLFLAGS size_t oms_read_header_mp4(uint8_t* buffer, size_t buffer_offset, size_t buffer_size, oms_header_t* header_out);

    // Read an oms_sequence_t from the buffer into an oms_sequence_t struct and returns number of bytes read.
    LIB_OMS_DLLFLAGS size_t oms_read_sequence(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in, oms_sequence_t* sequence_out);

    // Parses the bytes in a buffer until the OMS data is found (this is used for when OMS data is packaged in an mp4)
    // Read an oms_sequence_t from the buffer into an oms_sequence_t struct and returns number of bytes read.
    LIB_OMS_DLLFLAGS size_t oms_read_sequence_mp4(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in, oms_sequence_t* sequence_out);

    // Returns the size in bytes of the header from the buffer.
    LIB_OMS_DLLFLAGS size_t oms_get_header_read_size(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size);

    // Returns the size in bytes of the header that will be output from oms_write_header.
    LIB_OMS_DLLFLAGS size_t oms_get_header_write_size(oms_header_t* header_in);

    // Returns the size in bytes of the sequence that will be output from oms_read_sequence.
    LIB_OMS_DLLFLAGS size_t oms_get_sequence_read_size(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size);

    // Returns the size in bytes of the sequence that will be output from oms_write_sequence.
    LIB_OMS_DLLFLAGS size_t oms_get_sequence_write_size(oms_header_t* header_in, oms_sequence_t* sequence_in);

    // Splits an oms sequence into two based on the split frame entered. returns split sequences to passed in params. option to discard normals if present
    LIB_OMS_DLLFLAGS void oms_split_sequence(oms_sequence_t* seq_in, int split_frame, bool discard_normals, oms_sequence_t** out_seq_a, oms_sequence_t** out_seq_b);

    // Writes an oms_header_t into the buffer and returns number of bytes written.
    LIB_OMS_DLLFLAGS size_t oms_write_header(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in);

    // Writes an oms_sequence_t into the buffer returns number of bytes written.
    LIB_OMS_DLLFLAGS size_t oms_write_sequence(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in, oms_sequence_t* sequence_in, oms_write_sequences_options_t* options);

    // Frees all the memory used in an oms_header_t.
    LIB_OMS_DLLFLAGS void oms_free_header(oms_header_t* header_in);

    // Frees all the memory used in an oms_sequence_t. Does not free the sequence itself!
    LIB_OMS_DLLFLAGS void oms_free_sequence(oms_sequence_t* sequence);

    // Internal. Sets bone count and allocates memory for a sequence's retargeting data
    LIB_OMS_DLLFLAGS void oms_alloc_retarget_data(oms_sequence_t* sequence, int frame_count, int num_bones);
    // Internal. frees memory for a sequence's retargeting data
    LIB_OMS_DLLFLAGS void oms_free_retarget_data(oms_sequence_t* sequence);

    // Sets the name of the given bone. Retarget data must have already been allocated.
    LIB_OMS_DLLFLAGS void oms_set_retarget_bone_name(oms_sequence_t* sequence, int bone, char* name);

    // Number of bytes per index entry, based on the vertex count that must be accommodated
    LIB_OMS_DLLFLAGS size_t oms_bytes_per_index(int vertex_count);

    // Allocates a new sequence and its nested memory structures.
    LIB_OMS_DLLFLAGS oms_sequence_t* oms_alloc_sequence(int vertex_count,
        int normal_count,
        int uv_count,
        int index_count,
        int frame_count,
        int ssdr_bone_count,
        int retarget_bone_count);

    // Computes the AABB for the sequence and stores the result in its aabb parameter.
    LIB_OMS_DLLFLAGS void oms_sequence_compute_aabb(oms_sequence_t* sequence);

    // Apply skinning to the keyframe. Required for one frame ssdr sequences as they will have no data written
    // or read, and so the transformation must be applied.
    LIB_OMS_DLLFLAGS void oms_apply_skinning(oms_sequence_t* sequence, oms_ssdr_frame_t ssdr_frame);

    // Copy keyframe from a sequence
    LIB_OMS_DLLFLAGS void oms_copy_keyframe(oms_sequence_t* src_seq, oms_sequence_t* dst_seq, bool discard_normals);

    // Copy ssdr frame from a sequence
    LIB_OMS_DLLFLAGS size_t oms_copy_ssdr_frame(oms_sequence_t* sequence, oms_sequence_t* chunk, int frameIndex, size_t ssdrSizeInChunk);

    // Mirror a sequence on x-axis for engines that require it.
    LIB_OMS_DLLFLAGS void oms_mirror_sequence_x(oms_sequence_t* sequence);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OMS_H
