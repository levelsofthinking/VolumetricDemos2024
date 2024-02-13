// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/oms.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <assert.h>

#define INCLUDE_GZIP 0
#define INCLUDE_ZSTD 0

#if INCLUDE_GZIP
#include <zlib.h>
#endif

#if INCLUDE_ZSTD
#include <zstd.h>
#endif

/* clang-format off */

// TODO:
//  - Check buffer bounds before reads.

// Reads data from SRC into DST. POSITION is a variable passed in that represents where in the SRC buffer to read from, it will automatically
// be advanced by the number of bytes read.
#define READ(DST, SRC, POSITION, TYPE, NUM_ELEMENTS) memcpy(&DST, &SRC[POSITION], sizeof(TYPE) * NUM_ELEMENTS); POSITION += sizeof(TYPE) * NUM_ELEMENTS;

// Writes data from SRC into DST. POSITION is a variable passed in that represents where in the DST buffer to write int, it will automatically
// be advanced by the number of bytes written.
#define WRITE(DST, DST_SIZE, POSITION, SRC, TYPE, NUM_ELEMENTS) memcpy(&DST[POSITION], &SRC, sizeof(TYPE) * NUM_ELEMENTS); POSITION += sizeof(TYPE) * NUM_ELEMENTS; assert(POSITION <= DST_SIZE);

// NOTE: Both of the above functions prepend SRC and DST with & to get their memory address. For arrays backed by pointers pass in the first element of
//       the array, otherwise it will read/write the address of the pointer instead of element it points to.

// Example Usage: 
//  int* dataIn = (int*)malloc(sizeof(int) * 5);
//  size_t dataOutSizeByte = sizeof(int) * 5;
//  int* dataOut = (int*)malloc(dataOutSizeBytes);
//  uint8_t* buffer = (uint8_t)malloc(sizeof(int) * 5);
//  int readPosition = 0;
//  READ(buffer[0], dataIn, readPosition, int, 5);
//  int writePosition = 0;
//  WRITE(dataOut, dataOutSizeBytes, writePosition, buffer[0], int, 5);

/* clang-format on */

size_t oms_read_header(uint8_t* buffer, size_t buffer_offset, size_t buffer_size, oms_header_t* header_out)
{
    size_t position = buffer_offset;

    READ(header_out->version, buffer, position, int, 1);

    // Check if the file version matches the lib version.
    if (header_out->version != OMS_VERSION)
    {
        return OMS_BAD_VERSION;
    }

    READ(header_out->sequence_count, buffer, position, int, 1);
    READ(header_out->has_retarget_data, buffer, position, bool, 1);
    READ(header_out->compression_level, buffer, position, uint8_t, 1);
    READ(header_out->frame_count, buffer, position, uint32_t, 1);

    int sequenceTableEntrySize = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t);
    header_out->sequence_table_entries = (sequence_table_entry*)malloc(sizeof(sequence_table_entry) * header_out->sequence_count);
    for (int i = 0; i < header_out->sequence_count; i++)
    {
        READ(header_out->sequence_table_entries[i].frame_count, buffer, position, uint32_t, 1);
        READ(header_out->sequence_table_entries[i].start_frame, buffer, position, uint32_t, 1);
        READ(header_out->sequence_table_entries[i].end_frame, buffer, position, uint32_t, 1);
        READ(header_out->sequence_table_entries[i].start_byte, buffer, position, uint64_t, 1);
        READ(header_out->sequence_table_entries[i].end_byte, buffer, position, uint64_t, 1);
    }

    return position - buffer_offset;
}

void oms_free_header(oms_header_t* header_in)
{
    free(header_in->sequence_table_entries);
}

float U16ToF32(uint16_t value)
{
    return (float)value / (float)UINT16_MAX;
}

size_t oms_bytes_per_index(int vertex_count)
{
    if (vertex_count <= (UINT16_MAX + 1))
    {
        return sizeof(uint16_t);
    }
    else
    {
        return sizeof(uint32_t);
    }
}

size_t oms_read_sequence(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in, oms_sequence_t* sequence_out)
{
    size_t position = buffer_offset;

    int sequenceSize = 0;
    READ(sequenceSize, buffer_in, position, int, 1);

    uint8_t* buffer = NULL;

    if (header_in->compression_level == OMS_COMPRESSION_NONE || header_in->compression_level == OMS_COMPRESSION_DELTA)
    {
        buffer = buffer_in;
    }

    if (header_in->compression_level == OMS_COMPRESSION_GZIP)
    {
#if INCLUDE_GZIP
        // Last 4 bytes are the decompressed size.
        uint32_t decompressedSize = 0;
        int decompressedPos = position + sequenceSize - 4;
        READ(decompressedSize, buffer_in, decompressedPos, uint32_t, 1);

        buffer = (uint8_t*)malloc(decompressedSize);

        // Skip past the gzip header
        position += 10;

        z_stream defstream;
        defstream.zalloc = Z_NULL;
        defstream.zfree = Z_NULL;
        defstream.opaque = Z_NULL;

        defstream.avail_in = (uInt)sequenceSize - 14;
        defstream.next_in = &buffer_in[position];
        defstream.avail_out = (uInt)(decompressedSize);
        defstream.next_out = &buffer[0];

        inflateInit(&defstream, Z_BEST_COMPRESSION);
        inflate(&defstream, Z_FINISH);
        inflateEnd(&defstream);
#endif

        position = 0;
    }

    if (header_in->compression_level == OMS_COMPRESSION_ZSTD)
    {
#if INCLUDE_ZSTD
        unsigned long long const uncompressedSize = ZSTD_findDecompressedSize(&buffer_in[position], sequenceSize);
        buffer = (uint8_t*)malloc(uncompressedSize);
        size_t const dSize = ZSTD_decompress(&buffer[0], uncompressedSize, &buffer_in[position], sequenceSize);
#endif

        position = 0;
    }

    // Axis Aligned Bounding Box
    READ(sequence_out->aabb, buffer, position, oms_aabb_t, 1);

    // Calculate center point (used in delta decompression).
    oms_vec3_t centerPoint;
    centerPoint.x = (sequence_out->aabb.min.x + sequence_out->aabb.max.x) / 2.0f;
    centerPoint.y = (sequence_out->aabb.min.y + sequence_out->aabb.max.y) / 2.0f;
    centerPoint.z = (sequence_out->aabb.min.z + sequence_out->aabb.max.z) / 2.0f;

    // Read Vertices
    READ(sequence_out->vertex_count, buffer, position, int, 1);
    sequence_out->vertices = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * sequence_out->vertex_count);

    // Vertex Dequantization
    float xMin = 0.0f, xMult = 0.0f, yMin = 0.0f, yMult = 0.0f, zMin = 0.0f, zMult = 0.0f;
    READ(xMin, buffer, position, float, 1);
    READ(xMult, buffer, position, float, 1);
    READ(yMin, buffer, position, float, 1);
    READ(yMult, buffer, position, float, 1);
    READ(zMin, buffer, position, float, 1);
    READ(zMult, buffer, position, float, 1);

    // Multipliers are what was used to encode, inverse to get decoding multipliers
    xMult = 1.0f / xMult;
    yMult = 1.0f / yMult;
    zMult = 1.0f / zMult;

    int sizeOfVertices = 0;
    READ(sizeOfVertices, buffer, position, int, 1);

    uint8_t* vertData = &buffer[position];
    position += sizeOfVertices;

    int x = 0;
    int y = 0;
    int z = 0;
    int vertsRead = 0;

    for (int i = 0; i < sizeOfVertices;)
    {
        // Each number is stored as one or two bytes, determined by bit 7 of the first byte.
        // Leading bit 0: Small format. Number is stored as a delta, offset by +63
        // Leading bit 1: Extended format. Number is absolute, with high order in next byte
        // For speed this is duplicated per coord instead of being a series of function calls

        uint8_t x0 = vertData[i++];
        if ((x0 & 0x80) == 0)
        {
            x = x + (x0 - 63);
        }
        else
        {
            x = (x0 & 0x7F) | (vertData[i++] << 7);
        }

        uint8_t y0 = vertData[i++];
        if ((y0 & 0x80) == 0)
        {
            y = y + (y0 - 63);
        }
        else
        {
            y = (y0 & 0x7F) | (vertData[i++] << 7);
        }

        uint8_t z0 = vertData[i++];
        if ((z0 & 0x80) == 0)
        {
            z = z + (z0 - 63);
        }
        else
        {
            z = (z0 & 0x7F) | (vertData[i++] << 7);
        }

        sequence_out->vertices[vertsRead].x = x * xMult + xMin;
        sequence_out->vertices[vertsRead].y = y * yMult + yMin;
        sequence_out->vertices[vertsRead].z = z * zMult + zMin;

        vertsRead++;
    }

    // Normals
    READ(sequence_out->normal_count, buffer, position, int, 1);
    sequence_out->normals = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * sequence_out->normal_count);
    for (int i = 0; i < sequence_out->normal_count; ++i)
    {
        uint16_t compressedNormals[3];
        READ(compressedNormals, buffer, position, uint16_t, 3);

        sequence_out->normals[i].x = (U16ToF32(compressedNormals[0]) * 2.0f) - 1.0f;
        sequence_out->normals[i].y = (U16ToF32(compressedNormals[1]) * 2.0f) - 1.0f;
        sequence_out->normals[i].z = (U16ToF32(compressedNormals[2]) * 2.0f) - 1.0f;
    }

    // UVs
    sequence_out->uv_count = sequence_out->vertex_count;
    sequence_out->uvs = (oms_vec2_t*)malloc(sizeof(oms_vec2_t) * sequence_out->vertex_count);
    int sizeOfUVs = 0;
    READ(sizeOfUVs, buffer, position, int, 1);

    uint8_t* uvData = &buffer[position];
    position += sizeOfUVs;

    // Might end up reading this from OMS file, but for now is fixed
    uint8_t uvBitsPrecision = 12;

    int u = 0;
    int v = 0;
    int uvsRead = 0;
    float quantizedUVToFloatMult = 1.0f / ((1 << uvBitsPrecision) - 1);

    for (int i = 0; i < sizeOfUVs;)
    {
        // Each number is stored as one or two bytes, determined by bit 7 of the first byte.
        // Leading bit 0: Small format. Number is stored as a delta, offset by +63
        // Leading bit 1: Extended format. Number is absolute, with high order in next byte
        // For speed this is duplicated per coord instead of being a series of function calls

        // Read u coord
        uint8_t u0 = uvData[i++];
        if ((u0 & 0x80) == 0)
        {
            u = u + u0 - 63;
        }
        else
        {
            u = (u0 & 0x7F) | (uvData[i++] << 7);
        }

        // Read v coord
        uint8_t v0 = uvData[i++];
        if ((v0 & 0x80) == 0)
        {
            v = v + v0 - 63;
        }
        else
        {
            v = (v0 & 0x7F) | (uvData[i++] << 7);
        }

        sequence_out->uvs[uvsRead].x = u * quantizedUVToFloatMult;
        sequence_out->uvs[uvsRead].y = v * quantizedUVToFloatMult;

        uvsRead++;
    }

    // Indices
    READ(sequence_out->index_count, buffer, position, int, 1);

    size_t bpi = oms_bytes_per_index(sequence_out->vertex_count);
    sequence_out->indices = malloc(bpi * sequence_out->index_count);
    memcpy(sequence_out->indices, &buffer[position], bpi * sequence_out->index_count);
    position += bpi * sequence_out->index_count;

    // SSDR Bone Weights and Indices
    int boneWeightCount = 0;
    READ(boneWeightCount, buffer, position, int, 1);

    if (boneWeightCount > 0)
    {
        sequence_out->ssdr_bone_indices = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * boneWeightCount);
        sequence_out->ssdr_bone_weights = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * boneWeightCount);
        sequence_out->extras.ssdr_weights_packed = (int*)malloc(sizeof(int) * boneWeightCount);

        // Unpack the bone data into usable indices and weights
        for (int i = 0; i < boneWeightCount; ++i)
        {
            uint8_t boneIndex[4] = { 0, 0, 0, 0 };
            int boneWeights = 0;

            READ(boneIndex[0], buffer, position, uint8_t, 4);
            READ(boneWeights, buffer, position, int, 1);

            // Store packed weights
            sequence_out->extras.ssdr_weights_packed[i] = boneWeights;

            // Convert into usable data.
            sequence_out->ssdr_bone_indices[i].x = (float)boneIndex[0];
            sequence_out->ssdr_bone_indices[i].y = (float)boneIndex[1];
            sequence_out->ssdr_bone_indices[i].z = (float)boneIndex[2];
            sequence_out->ssdr_bone_indices[i].w = (float)boneIndex[3];

            const float boneWeightMult = 1.0f / ((1 << 11) - 1);
            const float smallBoneWeightMult = 0.5f / ((1 << 10) - 1); // For bones with weights in [0, 0.5]

            float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            weights[0] = (boneWeights & 0x7FF) * boneWeightMult;
            weights[1] = (boneWeights >> 11 & 0x3FF) * smallBoneWeightMult;
            weights[2] = (boneWeights >> 21) * smallBoneWeightMult;

            // Derive weight 3, since total weight sums to 1.
            weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

            // However, weight3 will accumulate quantization truncation (round-down) errors.
            // If weight3 is potentially only non-zero due to round-down error we discard it and transfer the weight to bone 0.
            // (Weight0 has max rounding error of 1/2047, weight2 and 3 have a max rounding error of 1/2046)
            if (weights[3] <= (3.0f / 2046.0f))
            {
                sequence_out->ssdr_bone_weights[i].x = weights[0] + weights[3];
                sequence_out->ssdr_bone_weights[i].y = weights[1];
                sequence_out->ssdr_bone_weights[i].z = weights[2];
                sequence_out->ssdr_bone_weights[i].w = 0.0f;
            }
            else
            {
                memcpy(&sequence_out->ssdr_bone_weights[i].x, weights, sizeof(weights));
            }
        }
    }
    else 
    {
        sequence_out->ssdr_bone_indices = NULL;
        sequence_out->ssdr_bone_weights = NULL;
        sequence_out->extras.ssdr_weights_packed = NULL;
    }

    // SSDR Frame Data
    READ(sequence_out->ssdr_frame_count, buffer, position, int, 1);
    READ(sequence_out->ssdr_bone_count, buffer, position, int, 1);

    if (sequence_out->ssdr_frame_count > 1)
    {
        sequence_out->ssdr_frames = (oms_ssdr_frame_t*)malloc(sizeof(oms_ssdr_frame_t) * sequence_out->ssdr_frame_count);

        for (int i = 0; i < sequence_out->ssdr_frame_count; ++i)
        {
            sequence_out->ssdr_frames[i].matrices = (oms_matrix4x4_t*)malloc(sizeof(oms_matrix4x4_t) * sequence_out->ssdr_bone_count);
            READ(sequence_out->ssdr_frames[i].matrices[0], buffer, position, oms_matrix4x4_t, sequence_out->ssdr_bone_count);
        }
    }
    else 
    {
        sequence_out->ssdr_frames = NULL;
    }

    // Delta Compression Data
    if (header_in->compression_level == OMS_COMPRESSION_DELTA)
    {
        READ(sequence_out->delta_frame_count, buffer, position, int, 1);
        sequence_out->delta_frames = (oms_delta_frame_t*)malloc(sizeof(oms_delta_frame_t) * sequence_out->delta_frame_count);

        if (sequence_out->delta_frame_count > 0)
        {
            for (int f = 0; f < sequence_out->delta_frame_count; ++f)
            {
                sequence_out->delta_frames[f].vertices = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * sequence_out->vertex_count);

                int sizeOfDeltaVertices = 0;
                READ(sizeOfDeltaVertices, buffer, position, int, 1);

                uint8_t* deltaVertData = &buffer[position];
                position += sizeOfDeltaVertices;

                x = 0;
                y = 0;
                z = 0;
                int deltaVertsRead = 0;

                for (int i = 0; i < sizeOfDeltaVertices;)
                {
                    // Each number is stored as one or two bytes, determined by bit 7 of the first byte.
                    // Leading bit 0: Small format. Number is stored as a delta, offset by +63
                    // Leading bit 1: Extended format. Number is absolute, with high order in next byte
                    // For speed this is duplicated per coord instead of being a series of function calls

                    uint8_t x0 = deltaVertData[i++];
                    if ((x0 & 0x80) == 0)
                    {
                        x = x + (x0 - 63);
                    }
                    else
                    {
                        x = (x0 & 0x7F) | (deltaVertData[i++] << 7);
                    }

                    uint8_t y0 = deltaVertData[i++];
                    if ((y0 & 0x80) == 0)
                    {
                        y = y + (y0 - 63);
                    }
                    else
                    {
                        y = (y0 & 0x7F) | (deltaVertData[i++] << 7);
                    }

                    uint8_t z0 = deltaVertData[i++];
                    if ((z0 & 0x80) == 0)
                    {
                        z = z + (z0 - 63);
                    }
                    else
                    {
                        z = (z0 & 0x7F) | (deltaVertData[i++] << 7);
                    }

                    sequence_out->delta_frames[f].vertices[deltaVertsRead].x = x * xMult + xMin;
                    sequence_out->delta_frames[f].vertices[deltaVertsRead].y = y * yMult + yMin;
                    sequence_out->delta_frames[f].vertices[deltaVertsRead].z = z * zMult + zMin;

                    // Correct the packing quirk.
                    oms_vec3_t* delta = &sequence_out->delta_frames[f].vertices[deltaVertsRead];
                    delta->x = (delta->x - centerPoint.x) * 2.0f;
                    delta->y = (delta->y - centerPoint.y) * 2.0f;
                    delta->z = (delta->z - centerPoint.z) * 2.0f;

                    // Apply delta so this vert buffer can be immediately uploaded.
                    delta->x = delta->x + sequence_out->vertices[deltaVertsRead].x;
                    delta->y = delta->y + sequence_out->vertices[deltaVertsRead].y;
                    delta->z = delta->z + sequence_out->vertices[deltaVertsRead].z;

                    deltaVertsRead++;
                }
            }
        }
    }
    else
    {
        sequence_out->delta_frame_count = 0;
    }

    // Retargetting Data
    if (header_in->has_retarget_data)
    {
        float boneWeightMult = 1.0f / ((1 << 11) - 1);
        float smallBoneWeightMult = 0.5f / ((1 << 10) - 1); // For bones with weights in [0, 0.5]

        sequence_out->retarget_data.weights = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * sequence_out->vertex_count);
        sequence_out->retarget_data.indices = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * sequence_out->vertex_count);

        for (int i = 0; i < sequence_out->ssdr_frame_count; ++i)
        {
            // One-time joint info: Count, name, and hierarchy
            if (i == 0)
            {
                READ(sequence_out->retarget_data.bone_count, buffer, position, int, 1);

                sequence_out->retarget_data.bone_names = (char**)malloc(sizeof(char*) * sequence_out->retarget_data.bone_count);
                sequence_out->retarget_data.bone_parents = (int*)malloc(sizeof(int) * sequence_out->retarget_data.bone_count);
                sequence_out->retarget_data.bone_positions = (oms_vec3_t**)malloc(sizeof(oms_vec3_t*) * sequence_out->ssdr_frame_count);
                sequence_out->retarget_data.bone_rotations = (oms_quaternion_t**)malloc(sizeof(oms_quaternion_t*) * sequence_out->ssdr_frame_count);

                for (int n = 0; n < sequence_out->retarget_data.bone_count; ++n)
                {
                    int stringSize = 0;
                    READ(stringSize, buffer, position, int, 1);

                    sequence_out->retarget_data.bone_names[n] = (char*)malloc(stringSize + 1);
                    READ(sequence_out->retarget_data.bone_names[n][0], buffer, position, char, stringSize);
                    sequence_out->retarget_data.bone_names[n][stringSize] = '\0';

                    READ(sequence_out->retarget_data.bone_parents[n], buffer, position, int, 1);
                }
            }

            // Local position and rotation for each bone.
            sequence_out->retarget_data.bone_positions[i] = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * sequence_out->retarget_data.bone_count);
            sequence_out->retarget_data.bone_rotations[i] = (oms_quaternion_t*)malloc(sizeof(oms_quaternion_t) * sequence_out->retarget_data.bone_count);
            for (int n = 0; n < sequence_out->retarget_data.bone_count; ++n)
            {
                bool posKeyFrame = true;
                bool rotKeyFrame = true;

                bool anim_keyframe_compression = false;
                if (anim_keyframe_compression)
                {
                    uint8_t keyframe = sequence_out->retarget_data.keyframes[i][n];
                    posKeyFrame = (keyframe & kOMSKeyframePositionMask);
                    rotKeyFrame = (keyframe & kOMSKeyframeRotationMask);
                }
                else
                {
                    sequence_out->retarget_data.keyframes = NULL;
                }

                if (posKeyFrame)
                {
                    READ(sequence_out->retarget_data.bone_positions[i][n], buffer, position, oms_vec3_t, 1);
                }
                else
                {
                    sequence_out->retarget_data.bone_positions[i][n].x = 0.0f;
                    sequence_out->retarget_data.bone_positions[i][n].y = 0.0f;
                    sequence_out->retarget_data.bone_positions[i][n].z = 0.0f;
                }

                if (rotKeyFrame)
                {
                    READ(sequence_out->retarget_data.bone_rotations[i][n], buffer, position, oms_quaternion_t, 1);
                }
                else
                {
                    sequence_out->retarget_data.bone_rotations[i][n].x = 0.0f;
                    sequence_out->retarget_data.bone_rotations[i][n].y = 0.0f;
                    sequence_out->retarget_data.bone_rotations[i][n].z = 0.0f;
                }
            }
        }

        // Rigging bone vert weights -- 4 indices + weights per vert
        for (int i = 0; i < sequence_out->vertex_count; ++i)
        {
            float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            uint8_t boneIndex0and1;
            uint8_t boneIndex2and3;
            int weightsEncoded;

            READ(boneIndex0and1, buffer, position, uint8_t, 1);
            READ(boneIndex2and3, buffer, position, uint8_t, 1);
            READ(weightsEncoded, buffer, position, int, 1);

            sequence_out->retarget_data.indices[i].x = boneIndex0and1 & 0x0F;
            sequence_out->retarget_data.indices[i].y = boneIndex0and1 >> 4;
            sequence_out->retarget_data.indices[i].z = boneIndex2and3 & 0x0F;
            sequence_out->retarget_data.indices[i].w = boneIndex2and3 >> 4;

            weights[0] = (weightsEncoded & 0x7FF) * boneWeightMult;
            weights[1] = (weightsEncoded >> 11 & 0x3FF) * smallBoneWeightMult;
            weights[2] = (weightsEncoded >> 21) * smallBoneWeightMult;

            // Derive weight3, since total weight sums to 1.
            weights[3] = 1 - weights[0] - weights[1] - weights[2];

            // However, weights[3] will accumulate quantization truncation (round-down) errors.
            // If weights[3] is potentially only non-zero due to round-down error we discard it and transfer the weight to bone 0.
            // (weights[0] has max rounding error of 1/2047, weights[2] and weights[3] have a max rounding error of 1/2046)
            if (weights[3] <= (3.0 / 2046.0f))
            {
                sequence_out->retarget_data.weights[i].x = weights[0] + weights[3];
                sequence_out->retarget_data.weights[i].y = weights[1];
                sequence_out->retarget_data.weights[i].z = weights[2];
                sequence_out->retarget_data.weights[i].w = 0.0f;
            }
            else
            {
                sequence_out->retarget_data.weights[i].x = weights[0];
                sequence_out->retarget_data.weights[i].y = weights[1];
                sequence_out->retarget_data.weights[i].z = weights[2];
                sequence_out->retarget_data.weights[i].w = weights[3];
            }
        }
    }
    else
    {
        sequence_out->retarget_data.bone_count = 0;
        sequence_out->retarget_data.weights = NULL;
        sequence_out->retarget_data.indices = NULL;
        sequence_out->retarget_data.bone_names = NULL;
        sequence_out->retarget_data.bone_parents = NULL;
        sequence_out->retarget_data.keyframes = NULL;
        sequence_out->retarget_data.bone_positions = NULL;
        sequence_out->retarget_data.bone_rotations = NULL;
    }

    if (header_in->compression_level == OMS_COMPRESSION_GZIP || header_in->compression_level == OMS_COMPRESSION_ZSTD)
    {
        free(buffer);
    }

    return sequenceSize + 4;
}

void oms_copy_keyframe(oms_sequence_t* src_seq, oms_sequence_t* dst_seq, bool discard_normals)
{
    dst_seq->aabb = src_seq->aabb;

    dst_seq->vertex_count = src_seq->vertex_count;
    size_t vertex_size = sizeof(oms_vec3_t) * dst_seq->vertex_count;
    dst_seq->vertices = (oms_vec3_t*)malloc(vertex_size);
    memcpy(dst_seq->vertices, src_seq->vertices, vertex_size);

    if (discard_normals)
    {
        dst_seq->normal_count = 0;
        dst_seq->normals = NULL;
    }
    else
    {
        dst_seq->normal_count = src_seq->normal_count;
        size_t normal_size = sizeof(oms_vec3_t) * dst_seq->normal_count;
        dst_seq->normals = (oms_vec3_t*)malloc(normal_size);
        memcpy(dst_seq->normals, src_seq->normals, normal_size);
    }

    dst_seq->uv_count = src_seq->uv_count;
    size_t uv_size = sizeof(oms_vec2_t) * dst_seq->uv_count;
    dst_seq->uvs = (oms_vec2_t*)malloc(uv_size);
    memcpy(dst_seq->uvs, src_seq->uvs, uv_size);

    dst_seq->index_count = src_seq->index_count;
    size_t index_size;
    if (src_seq->vertex_count <= (UINT16_MAX + 1))
    {
        index_size = sizeof(uint16_t) * dst_seq->index_count;
    }
    else
    {
        index_size = sizeof(uint32_t) * dst_seq->index_count;
    }
    dst_seq->indices = malloc(index_size);
    memcpy(dst_seq->indices, src_seq->indices, index_size);

    // Set this to 0 as we will be filling in the SSDR frames manually
    dst_seq->ssdr_frame_count = 0;
    dst_seq->ssdr_bone_count = src_seq->ssdr_bone_count;

    if (src_seq->ssdr_frame_count > 1)
    {
        size_t bone_index_size = sizeof(oms_vec4_t) * dst_seq->vertex_count;
        dst_seq->ssdr_bone_indices = (oms_vec4_t*)malloc(bone_index_size);
        memcpy(dst_seq->ssdr_bone_indices, src_seq->ssdr_bone_indices, bone_index_size);

        size_t bone_weight_size = sizeof(oms_vec4_t) * dst_seq->vertex_count;
        dst_seq->ssdr_bone_weights = (oms_vec4_t*)malloc(bone_weight_size);
        memcpy(dst_seq->ssdr_bone_weights, src_seq->ssdr_bone_weights, bone_weight_size);

        if (src_seq->extras.ssdr_weights_packed != NULL)
        {
            size_t packed_bone_weight_size = sizeof(int) * dst_seq->vertex_count;
            dst_seq->extras.ssdr_weights_packed = (int*)malloc(packed_bone_weight_size);
            memcpy(dst_seq->extras.ssdr_weights_packed, src_seq->extras.ssdr_weights_packed, packed_bone_weight_size);
        }
    }

    dst_seq->ssdr_frames = (oms_ssdr_frame_t*)malloc(sizeof(oms_ssdr_frame_t));
    dst_seq->ssdr_frames->matrices = (oms_matrix4x4_t*)malloc(sizeof(oms_matrix4x4_t));

    dst_seq->retarget_data = src_seq->retarget_data;
}

size_t oms_copy_ssdr_frame(oms_sequence_t* src_seq, oms_sequence_t* dest_seq, int frame_index, size_t ssdr_size)
{
    size_t matrix_size = src_seq->ssdr_bone_count * sizeof(oms_matrix4x4_t);
    size_t extra_size = matrix_size + ssdr_size;
    oms_ssdr_frame_t* expanded_ssdr_frames = (oms_ssdr_frame_t*)realloc(dest_seq->ssdr_frames, ((dest_seq->ssdr_frame_count + 1) * sizeof(oms_ssdr_frame_t)));
    dest_seq->ssdr_frames = expanded_ssdr_frames;
    dest_seq->ssdr_frames[dest_seq->ssdr_frame_count].matrices = (oms_matrix4x4_t*)malloc(matrix_size);

    dest_seq->ssdr_frame_count = dest_seq->ssdr_frame_count + 1;
    if (src_seq->ssdr_frame_count > 1)
    {
        oms_ssdr_frame_t* seq_frame = &src_seq->ssdr_frames[frame_index];

        memcpy(&dest_seq->ssdr_frames[dest_seq->ssdr_frame_count - 1].matrices[0],
            &src_seq->ssdr_frames[frame_index].matrices[0],
            matrix_size);
    }

    return extra_size;
}

// Adds b to a
oms_vec4_t oms_vec4_t_add(oms_vec4_t* a, oms_vec4_t* b)
{
    oms_vec4_t result;
    result.x = a->x + b->x;
    result.y = a->y + b->y;
    result.z = a->z + b->z;
    result.w = a->w + b->w;

    return result;
}

oms_vec4_t oms_vec4_t_scalar_mult(oms_vec4_t* a, float scalar)
{
    oms_vec4_t result;
    result.x = a->x * scalar;
    result.y = a->y * scalar;
    result.z = a->z * scalar;
    result.w = a->w * scalar;

    return result;
}

void oms_matrix4x4_t_oms_vec3_t_mult(oms_matrix4x4_t* m, oms_vec3_t* v, oms_vec4_t* result, float w)
{
    result->x = m->m[0] * v->x + m->m[4] * v->y + m->m[8] * v->z + m->m[12] * w;
    result->y = m->m[1] * v->x + m->m[5] * v->y + m->m[9] * v->z + m->m[13] * w;
    result->z = m->m[2] * v->x + m->m[6] * v->y + m->m[10] * v->z + m->m[14] * w;
    result->w = m->m[3] * v->x + m->m[7] * v->y + m->m[11] * v->z + m->m[15] * w;
}

void oms_apply_skinning(oms_sequence_t* sequence, oms_ssdr_frame_t ssdr_frame)
{
    uint8_t num_bones_per_vertex = 4;
    float* bone_indices = (float*)sequence->ssdr_bone_indices;
    float* bone_weights = (float*)sequence->ssdr_bone_weights;

    bool transform_normals = false;
    if (sequence->normal_count == sequence->vertex_count)
    {
        transform_normals = true;
    }

    oms_vec3_t old_position;
    oms_vec3_t old_normal;

    // Transform Vertices and optionally Normals.
    for (int i = 0; i < sequence->vertex_count; i++)
    {
        // Use 1.0 in w component so translation is applied.
        old_position.x = sequence->vertices[i].x;
        old_position.y = sequence->vertices[i].y;
        old_position.z = sequence->vertices[i].z;

        if (transform_normals)
        {
            // Use 0.0 in w component so translation isn't applied.
            old_normal.x = sequence->normals[i].x;
            old_normal.y = sequence->normals[i].y;
            old_normal.z = sequence->normals[i].z;
        }

        oms_vec4_t new_position;
        new_position.x = 0.0;
        new_position.y = 0.0;
        new_position.z = 0.0;
        new_position.w = 0.0;

        oms_vec4_t new_normal;
        new_normal.x = 0.0;
        new_normal.y = 0.0;
        new_normal.z = 0.0;
        new_normal.w = 0.0;

        for (int j = 0; j < num_bones_per_vertex; j++)
        {
            int bone_index = bone_indices[i * num_bones_per_vertex + j];
            float weight = bone_weights[i * num_bones_per_vertex + j];

            oms_matrix4x4_t* bone_matrix = (oms_matrix4x4_t*)ssdr_frame.matrices[bone_index].m;
            oms_vec4_t vertex_transformation;
            oms_matrix4x4_t_oms_vec3_t_mult(bone_matrix, &old_position, &vertex_transformation, 1.0);
            vertex_transformation = oms_vec4_t_scalar_mult(&vertex_transformation, weight);

            new_position = oms_vec4_t_add(&new_position, &vertex_transformation);

            if (transform_normals)
            {
                oms_vec4_t normal_transformation;
                oms_matrix4x4_t_oms_vec3_t_mult(bone_matrix, &old_normal, &normal_transformation, 0.0);
                normal_transformation = oms_vec4_t_scalar_mult(&normal_transformation, weight);

                new_normal = oms_vec4_t_add(&new_normal, &normal_transformation);
            }
        }

        sequence->vertices[i].x = new_position.x;
        sequence->vertices[i].y = new_position.y;
        sequence->vertices[i].z = new_position.z;

        if (transform_normals)
        {
            sequence->normals[i].x = new_normal.x;
            sequence->normals[i].y = new_normal.y;
            sequence->normals[i].z = new_normal.z;
        }
    }
}

void oms_split_sequence(oms_sequence_t* seq_in, int split_frame, bool discard_normals, oms_sequence_t** out_seq_a, oms_sequence_t** out_seq_b)
{
    *out_seq_a = *out_seq_b = NULL;
    if (split_frame > seq_in->ssdr_frame_count - 1)
    {
        split_frame = seq_in->ssdr_frame_count; // if split frame is more than sequence length, we set it up so that the first part is the whole sequence
    }

    int ssdr_size = 0;

    // First sequence is 1 frame
    if (split_frame == 1)
    {
        *out_seq_a = oms_alloc_sequence(0, 0, 0, 0, 0, 0, 0);
        oms_copy_keyframe(seq_in, *out_seq_a, discard_normals);
        ssdr_size += oms_copy_ssdr_frame(seq_in, *out_seq_a, 0, ssdr_size);
        oms_ssdr_frame_t frame = (*out_seq_a)->ssdr_frames[0];
        oms_apply_skinning(*out_seq_a, frame);
        (*out_seq_a)->ssdr_frames[0] = frame;
        (*out_seq_a)->ssdr_frame_count = 1;
    }
    // Multi frame 1st split    (0 to splitframe-1)
    else
    {
        *out_seq_a = oms_alloc_sequence(0, 0, 0, 0, 0, 0, 0);
        oms_copy_keyframe(seq_in, *out_seq_a, discard_normals);
        for (int frame = 0; frame < split_frame; ++frame)
        {
            ssdr_size += oms_copy_ssdr_frame(seq_in, *out_seq_a, frame, ssdr_size);
        }
    }

    // Second sequence is 1 frame
    if (seq_in->ssdr_frame_count - split_frame == 1)
    {
        *out_seq_b = oms_alloc_sequence(0, 0, 0, 0, 0, 0, 0);
        oms_copy_keyframe(seq_in, *out_seq_b, discard_normals);
        ssdr_size += oms_copy_ssdr_frame(seq_in, *out_seq_b, 0, ssdr_size);
        oms_ssdr_frame_t frame = (*out_seq_b)->ssdr_frames[0];
        oms_apply_skinning(*out_seq_b, frame);
        (*out_seq_b)->ssdr_frames[0] = frame;
        (*out_seq_b)->ssdr_frame_count = 1;
    }

    // Multi frame 2nd split (splitframe to end of sequence)
    else
    {
        *out_seq_b = oms_alloc_sequence(0, 0, 0, 0, 0, 0, 0);
        oms_copy_keyframe(seq_in, *out_seq_b, discard_normals);
        for (int frame = split_frame; frame < seq_in->ssdr_frame_count; ++frame)
        {
            ssdr_size += oms_copy_ssdr_frame(seq_in, *out_seq_b, frame, ssdr_size);
        }
    }

    if (*out_seq_a)
    {
        (*out_seq_a)->retarget_data.bone_count = 0;
    }
    if (*out_seq_b)
    {
        (*out_seq_b)->retarget_data.bone_count = 0;
    }
}

size_t oms_get_header_write_size(oms_header_t* header_in)
{
    // Version:           int, 4 bytes
    // Sequence Count:    int, 4 bytes
    // Retarget Data:     bool, 1 byte
    // Compression Level: unsigned byte, 1 byte.
    // Frame count:       uint32_t, 4 bytes
    // Sequence Table:    28 * (Sequence Count)

    return 14 + (28 * header_in->sequence_count);
}

size_t oms_get_header_read_size(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size)
{
    // Skip past version.
    size_t position = buffer_offset + 4;

    int sequenceCount;
    READ(sequenceCount, buffer_in, position, int, 1);

    return 14 + (28 * sequenceCount);
}

oms_vec3_t get_quantizer_multiplier(oms_header_t* header_in, oms_sequence_t* sequence_in)
{
    int posBitsPrecision[3] = { 1, 1, 1 };
    float maxVertPosError = 0.0005f; // TODO: allow this setting to be passed in.

    for (int axis = 0; axis < 3; axis++)
    {
        float range = (sequence_in->aabb.max.data[axis] - sequence_in->aabb.min.data[axis]);
        while (posBitsPrecision[axis] < 15 && range / ((1 << posBitsPrecision[axis]) - 1) > maxVertPosError)
        {
            posBitsPrecision[axis] += 1;
        }
    }

    oms_vec3_t quantizerMults;
    for (int axis = 0; axis < 3; axis++)
    {
        quantizerMults.data[axis] = ((1 << posBitsPrecision[axis]) - 1) / (sequence_in->aabb.max.data[axis] - sequence_in->aabb.min.data[axis]);
    }

    return quantizerMults;
}

// Packs successive 15-bit values ("uint15", stored in uint16s) as 1-byte deltas when possible and 2 byte values when not.
// Each value takes the following form:
// Byte 0:
//  Bit 7: Extended flag -- 0 for delta, 1 for full/extended value
//  Bit 0-6: If delta, delta value in range [-63, 63] offset by +63 (i.e. stored as [0, 126])
//         If extended, low 7 bits of uint16.
// Byte 1 (Extended byte, only exists of extended flag set):
//  Bit 0-7: Bits 8-14 of uint15
// Adds the resulting byte(s) to the back of the given vector. Byte 1 will follow byte 0, if it exists.
uint8_t compress_uint16_t(const uint16_t value, const uint16_t lastValue, uint8_t* result)
{
    if (abs(value - (int)lastValue) < 64)
    {
        // Leading bit 0: Small Format
        // Add 63 to get unsigned range [-63, 63] into [0, 0x7E]
        unsigned char u0 = value - lastValue + 63;
        result[0] = u0;

        return 1;
    }
    else
    {
        // Leading bit 1 for Extended Format, rest as normal
        result[0] = 0x80 | (value & 0x7F);
        // Extended byte is just higher order bits
        result[1] = value >> 7;

        return 2;
    }

    return 0;
}

size_t oms_get_sequence_read_size(uint8_t* buffer_in, size_t buffer_offset, size_t buffer_size)
{
    size_t position = buffer_offset;

    int sequence_size = 0;
    READ(sequence_size, buffer_in, position, int, 1);

    return sequence_size + 4;
}

size_t oms_get_sequence_write_size(oms_header_t* header_in, oms_sequence_t* sequence_in)
{
    size_t result = 0;

    // Sequence Size:     int, 4 bytes
    // AABB:              6 floats, 24 bytes
    // Vertex Count:      int, 4 bytes
    // Vertex Quant Data: 6 floats, 24 bytes
    // Size of Vertices:  int, 4 bytes

    result += 60;

    // Hack(kbostelmann): Calculate the bounding volume because it impacts
    // compression and the total sequence size. This is a side effect as it
    // mutates the sequence.
    oms_sequence_compute_aabb(sequence_in);

    oms_vec3_t centerPoint;
    centerPoint.x = (sequence_in->aabb.min.x + sequence_in->aabb.max.x) / 2.0f;
    centerPoint.y = (sequence_in->aabb.min.y + sequence_in->aabb.max.y) / 2.0f;
    centerPoint.z = (sequence_in->aabb.min.z + sequence_in->aabb.max.z) / 2.0f;

    oms_vec3_t quantizerMults = get_quantizer_multiplier(header_in, sequence_in);
    uint16_t lastPosValue[3] = { 0, 0, 0 };
    uint8_t posBytes[2];
    for (int n = 0; n < sequence_in->vertex_count; ++n)
    {
        oms_vec3_t* position = &sequence_in->vertices[n];
        for (int axis = 0; axis < 3; axis++)
        {
            uint16_t posValue = (position->data[axis] - sequence_in->aabb.min.data[axis]) * quantizerMults.data[axis];
            uint8_t byteCount = compress_uint16_t(posValue, lastPosValue[axis], &posBytes[0]);
            lastPosValue[axis] = posValue;

            result += byteCount;
        }
    }

    // Normals Size: int, 4 bytes.
    result += 4;

    // 6 bytes per normal.
    result += (6 * sequence_in->normal_count);

    // UVS
    uint8_t uBytes[2];
    uint8_t vBytes[2];
    uint16_t lastU = 0, lastV = 0;
    int uvBitsPrecision = 12; // 12 bits = enough for per-pixel addressing of 4K x 4K texture TODO: Make dynamic based on max texture size passed in
    uint16_t uvToShortMult = (1 << uvBitsPrecision) - 1;

    for (int n = 0; n < sequence_in->uv_count; ++n)
    {
        uint16_t u = sequence_in->uvs[n].x * uvToShortMult;
        uint16_t v = sequence_in->uvs[n].y * uvToShortMult;

        uint8_t uByteCount = compress_uint16_t(u, lastU, &uBytes[0]);
        lastU = u;
        uint8_t vByteCount = compress_uint16_t(v, lastV, &vBytes[0]);
        lastV = v;

        result += uByteCount + vByteCount;
    }

    // UV Size
    result += 4;

    // Triangles Size: 4 bytes.
    result += 4;

    // Indices size
    result += oms_bytes_per_index(sequence_in->vertex_count) * sequence_in->index_count;

    if (sequence_in->ssdr_frame_count > 1)
    {
        if (sequence_in->ssdr_bone_count > 0)
        {
            // SSDR Bone Weight Count: 4 bytes
            result += 4;

            // Each SSDR bone weight entry is 8 bytes.
            result += (8 * sequence_in->vertex_count);

            // SSDR Frame Count: 4 bytes
            // SSDR Bone Count: 4 bytes
            result += 8;

            // Each SSDR frame is a matrix of 16 floats: 64 bytes
            result += (64 * sequence_in->ssdr_bone_count * sequence_in->ssdr_frame_count);
        }
        else
        {
            // SSDR Vert Count: 4 bytes
            // SSDR Frame Count: 4 bytes
            // SSDR Bone Count: 4 bytes
            result += 12;
        }
    }
    else
    {
        // SSDR Vert Count: 4 bytes
        // SSDR Frame Count: 4 bytes
        // SSDR Bone Count: 4 bytes
        result += 12;
    }

    // Delta compression.
    if (header_in->compression_level == OMS_COMPRESSION_DELTA)
    {
        // Delta frame count.
        result += 4;

        if (sequence_in->delta_frame_count > 0)
        {
            for (int i = 0; i < sequence_in->delta_frame_count; ++i)
            {
                // Size of Delta Vertices:  int, 4 bytes
                result += 4;

                // Compute the compressed size of delta vertices.
                memset(lastPosValue, 0, sizeof(uint16_t) * 3);

                for (int n = 0; n < sequence_in->vertex_count; ++n)
                {
                    oms_vec3_t delta = sequence_in->delta_frames[i].vertices[n];

                    delta.x = (delta.x / 2.0f) + centerPoint.x;
                    delta.y = (delta.y / 2.0f) + centerPoint.y;
                    delta.z = (delta.z / 2.0f) + centerPoint.z;

                    for (int axis = 0; axis < 3; axis++)
                    {
                        uint16_t posValue = (uint16_t)((delta.data[axis] - sequence_in->aabb.min.data[axis]) * quantizerMults.data[axis]);
                        uint8_t byteCount = compress_uint16_t(posValue, lastPosValue[axis], &posBytes[0]);
                        lastPosValue[axis] = posValue;

                        result += byteCount;
                    }
                }
            }
        }
    }

    // Retarget Data
    if (header_in->has_retarget_data)
    {
        for (int i = 0; i < sequence_in->ssdr_frame_count; ++i)
        {
            if (i == 0)
            {
                // Bone count.
                result += sizeof(int32_t);

                for (int n = 0; n < sequence_in->retarget_data.bone_count; ++n)
                {
                    // String Size: 4 bytes
                    // String Itself: Variable Size
                    // Bone Parent: 4 bytes

                    int stringSize = strlen(sequence_in->retarget_data.bone_names[n]);
                    result += 4 + stringSize + 4;
                }
            }

            for (int j = 0; j < sequence_in->retarget_data.bone_count; ++j)
            {
                bool posKeyFrame = true;
                bool rotKeyFrame = true;

                bool anim_keyframe_compression = false;
                if (false)
                {
                    uint8_t keyframe = sequence_in->retarget_data.keyframes[i][j];
                    posKeyFrame = (keyframe & kOMSKeyframePositionMask);
                    rotKeyFrame = (keyframe & kOMSKeyframeRotationMask);

                    // Keyframe Flags
                    result += sizeof(uint8_t);
                }

                if (posKeyFrame)
                {
                    // Bone Position: 12 bytes
                    result += 12;
                }

                if (rotKeyFrame)
                {
                    // Bone Rotation: 16 bytes
                    result += 16;
                }
            }
        }

        // Bone Indices: 2 bytes / vert
        // Bone Weights: 4 bytes / vert
        result += sequence_in->vertex_count * (2 + 4);
    }

    return result;
}

size_t oms_write_header(uint8_t* buffer, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in)
{
    size_t position = buffer_offset;

    WRITE(buffer, buffer_size, position, header_in->version, int, 1);
    WRITE(buffer, buffer_size, position, header_in->sequence_count, int, 1);
    WRITE(buffer, buffer_size, position, header_in->has_retarget_data, bool, 1);
    WRITE(buffer, buffer_size, position, header_in->compression_level, uint8_t, 1);
    WRITE(buffer, buffer_size, position, header_in->frame_count, uint32_t, 1);

    for (int i = 0; i < header_in->sequence_count; i++)
    {
        WRITE(buffer, buffer_size, position, header_in->sequence_table_entries[i].frame_count, uint32_t, 1);
        WRITE(buffer, buffer_size, position, header_in->sequence_table_entries[i].start_frame, uint32_t, 1);
        WRITE(buffer, buffer_size, position, header_in->sequence_table_entries[i].end_frame, uint32_t, 1);
        WRITE(buffer, buffer_size, position, header_in->sequence_table_entries[i].start_byte, uint64_t, 1);
        WRITE(buffer, buffer_size, position, header_in->sequence_table_entries[i].end_byte, uint64_t, 1);
    }

    return position - buffer_offset;
}

uint16_t F32ToU16(float value)
{
    return (uint16_t)(UINT16_MAX * value);
}

size_t oms_write_sequence(uint8_t* buffer_out, size_t buffer_offset, size_t buffer_size, oms_header_t* header_in, oms_sequence_t* sequence_in, oms_write_sequences_options_t* options)
{
    // Recompute the AABB for the sequence
    oms_sequence_compute_aabb(sequence_in);

    size_t position = 0;
    size_t buffer_sequence_size = oms_get_sequence_write_size(header_in, sequence_in);
    uint8_t* buffer = (uint8_t*)malloc(buffer_sequence_size);

    // Sequence size is written at the bottom of this function.

    // AABB
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.min.x, float, 1);
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.min.y, float, 1);
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.min.z, float, 1);
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.max.x, float, 1);
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.max.y, float, 1);
    WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.max.z, float, 1);

    oms_vec3_t centerPoint;
    centerPoint.x = (sequence_in->aabb.min.x + sequence_in->aabb.max.x) / 2.0f;
    centerPoint.y = (sequence_in->aabb.min.y + sequence_in->aabb.max.y) / 2.0f;
    centerPoint.z = (sequence_in->aabb.min.z + sequence_in->aabb.max.z) / 2.0f;

    // Vertices
    WRITE(buffer, buffer_sequence_size, position, sequence_in->vertex_count, int, 1);

    oms_vec3_t quantizerMults = get_quantizer_multiplier(header_in, sequence_in);
    for (int axis = 0; axis < 3; axis++)
    {
        WRITE(buffer, buffer_sequence_size, position, sequence_in->aabb.min.data[axis], float, 1);
        WRITE(buffer, buffer_sequence_size, position, quantizerMults.data[axis], float, 1);
    }

    // Worse case scenario is all 2 byte entries.
    uint8_t* vertexBuffer = (uint8_t*)malloc(sequence_in->vertex_count * 3 * 2);
    int vertexBufferPos = 0;

    uint16_t lastPosValue[3] = { 0, 0, 0 };
    uint8_t posBytes[2];
    for (int n = 0; n < sequence_in->vertex_count; ++n)
    {
        oms_vec3_t* pos = &sequence_in->vertices[n];
        for (int axis = 0; axis < 3; axis++)
        {
            uint16_t posValue = (uint16_t)((pos->data[axis] - sequence_in->aabb.min.data[axis]) * quantizerMults.data[axis]);
            uint8_t byteCount = compress_uint16_t(posValue, lastPosValue[axis], &posBytes[0]);
            lastPosValue[axis] = posValue;

            WRITE(vertexBuffer, buffer_sequence_size, vertexBufferPos, posBytes, uint8_t, byteCount);
        }
    }

    // Write the vertex data out along with it's size in bytes.
    WRITE(buffer, buffer_sequence_size, position, vertexBufferPos, int, 1);

    WRITE(buffer, buffer_sequence_size, position, vertexBuffer[0], uint8_t, vertexBufferPos);
    free(vertexBuffer);

    // Normals
    WRITE(buffer, buffer_sequence_size, position, sequence_in->normal_count, int, 1);
    for (int n = 0; n < sequence_in->normal_count; ++n)
    {
        uint16_t normals[] = { F32ToU16(sequence_in->normals[n].x * 0.5f + 0.5f),
                               F32ToU16(sequence_in->normals[n].y * 0.5f + 0.5f),
                               F32ToU16(sequence_in->normals[n].z * 0.5f + 0.5f) };
        WRITE(buffer, buffer_sequence_size, position, normals, uint16_t, 3);
    }

    // UVs
    uint8_t* uvBuffer = (uint8_t*)malloc(sequence_in->uv_count * 2 * 2);
    int uvBufferPos = 0;

    uint8_t uBytes[2];
    uint8_t vBytes[2];
    uint16_t lastU = 0, lastV = 0;
    int uvBitsPrecision = 12; // 12 bits = enough for per-pixel addressing of 4K x 4K texture TODO: Make dynamic based on max texture size passed in
    uint16_t uvToShortMult = (1 << uvBitsPrecision) - 1;

    for (int n = 0; n < sequence_in->uv_count; ++n)
    {
        uint16_t u = sequence_in->uvs[n].x * uvToShortMult;
        uint16_t v = sequence_in->uvs[n].y * uvToShortMult;

        uint8_t uByteCount = compress_uint16_t(u, lastU, &uBytes[0]);
        lastU = u;
        uint8_t vByteCount = compress_uint16_t(v, lastV, &vBytes[0]);
        lastV = v;

        WRITE(uvBuffer, buffer_sequence_size, uvBufferPos, uBytes, uint8_t, uByteCount);
        WRITE(uvBuffer, buffer_sequence_size, uvBufferPos, vBytes, uint8_t, vByteCount);
    }

    // Write out UV buffer size and data.
    WRITE(buffer, buffer_sequence_size, position, uvBufferPos, int, 1);
    WRITE(buffer, buffer_sequence_size, position, uvBuffer[0], uint8_t, uvBufferPos);
    free(uvBuffer);

    // Triangles
    WRITE(buffer, buffer_sequence_size, position, sequence_in->index_count, int, 1);
    if (oms_bytes_per_index(sequence_in->vertex_count) == 2)
    {
        uint16_t* indices = (uint16_t*)sequence_in->indices;
        for (int n = 0; n < sequence_in->index_count; ++n)
        {
            WRITE(buffer, buffer_sequence_size, position, indices[n], uint16_t, 1);
        }
    }
    else
    {
        uint32_t* indices = (uint32_t*)sequence_in->indices;
        for (int n = 0; n < sequence_in->index_count; ++n)
        {
            WRITE(buffer, buffer_sequence_size, position, indices[n], uint32_t, 1);
        }
    }

    // SSDR
    if (sequence_in->ssdr_frame_count > 1)
    {
        if (sequence_in->ssdr_bone_count > 0)
        {
            uint8_t boneIndices[4];
            float boneWeights[4];

            const int boneWeightTo11BitsMult = (1 << 11) - 1;
            const int smallBoneWeightTo10BitsMult = 2 * ((1 << 10) - 1); //For bone weights <= 0.5

            WRITE(buffer, buffer_sequence_size, position, sequence_in->vertex_count, int, 1);

            for (int n = 0; n < sequence_in->vertex_count; ++n)
            {
                float totalWeight = 0.0f;
                for (int i = 0; i < 4; ++i)
                {
                    boneIndices[i] = sequence_in->ssdr_bone_indices[n].data[i];

                    // Clamp any negative weights.
                    boneWeights[i] = sequence_in->ssdr_bone_weights[n].data[i] < 0.0f ? 0.0f : sequence_in->ssdr_bone_weights[n].data[i];

                    totalWeight += boneWeights[i];
                }

                // Sort by weight descending (quadratically!... but bonecount is small)
                // [Note: Bones should already be sorted -- this is defensive/redundant]
                for (int i = 0; i < 4; ++i)
                {
                    for (int j = i + 1; j < 4; ++j)
                    {
                        if (boneWeights[j] > boneWeights[i])
                        {
                            uint8_t swapBone = boneIndices[i];
                            float swapWeight = boneWeights[i];
                            boneIndices[i] = boneIndices[j];
                            boneWeights[i] = boneWeights[j];
                            boneIndices[j] = swapBone;
                            boneWeights[j] = swapWeight;
                        }
                    }
                }

                // Write out bone indices
                WRITE(buffer, buffer_sequence_size, position, boneIndices, uint8_t, 4);

                if (options != NULL && options->use_packed_ssdr_weights && sequence_in->extras.ssdr_weights_packed != NULL)
                {
                    // If enabled we write back out the same weight integer we read in.
                    // This avoids precision loss from unpacking and repacking.
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->extras.ssdr_weights_packed[n], int, 1);
                }
                else
                {
                    // Write out weights normalized except last weight since it can be derived
                    int weight = (int)(boneWeights[0] / totalWeight * boneWeightTo11BitsMult)
                        | (int)(boneWeights[1] / totalWeight * smallBoneWeightTo10BitsMult) << 11
                        | (int)(boneWeights[2] / totalWeight * smallBoneWeightTo10BitsMult) << 21;

                    WRITE(buffer, buffer_sequence_size, position, weight, int, 1);
                }
            }

            // Animation Frames
            WRITE(buffer, buffer_sequence_size, position, sequence_in->ssdr_frame_count, int, 1);
            WRITE(buffer, buffer_sequence_size, position, sequence_in->ssdr_bone_count, int, 1);

            for (int i = 0; i < sequence_in->ssdr_frame_count; ++i)
            {
                for (int j = 0; j < sequence_in->ssdr_bone_count; ++j)
                {
                    // TODO: Transpose or nah?
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->ssdr_frames[i].matrices[j].m, float, 16);
                }
            }
        }
        // Writes for Retarget w/o SSDR.
        else
        {
            int ssdr_verts = 0;
            WRITE(buffer, buffer_sequence_size, position, ssdr_verts, int, 1);
            WRITE(buffer, buffer_sequence_size, position, sequence_in->ssdr_frame_count, int, 1);
            WRITE(buffer, buffer_sequence_size, position, sequence_in->ssdr_bone_count, int, 1);
        }
    }
    else
    {
        int ssdr_verts = 0;
        int ssdr_frames = 1;
        int ssdr_bones = 0;

        WRITE(buffer, buffer_sequence_size, position, ssdr_verts, int, 1);
        WRITE(buffer, buffer_sequence_size, position, ssdr_frames, int, 1);
        WRITE(buffer, buffer_sequence_size, position, ssdr_bones, int, 1);
    }

    // Delta compression.
    if (header_in->compression_level == OMS_COMPRESSION_DELTA)
    {
        WRITE(buffer, buffer_sequence_size, position, sequence_in->delta_frame_count, int, 1);

        uint8_t* deltaVertexBuffer = (uint8_t*)malloc(sequence_in->vertex_count * 3 * 2);

        for (int f = 0; f < sequence_in->delta_frame_count; ++f)
        {
            int deltaVertexBufferPos = 0;

            memset(lastPosValue, 0, sizeof(uint16_t) * 3);
            for (int n = 0; n < sequence_in->vertex_count; ++n)
            {
                oms_vec3_t delta = sequence_in->delta_frames[f].vertices[n];

                delta.x = (delta.x / 2.0f) + centerPoint.x;
                delta.y = (delta.y / 2.0f) + centerPoint.y;
                delta.z = (delta.z / 2.0f) + centerPoint.z;

                for (int axis = 0; axis < 3; axis++)
                {
                    uint16_t posValue = (uint16_t)((delta.data[axis] - sequence_in->aabb.min.data[axis]) * quantizerMults.data[axis]);
                    uint8_t byteCount = compress_uint16_t(posValue, lastPosValue[axis], &posBytes[0]);
                    lastPosValue[axis] = posValue;

                    WRITE(deltaVertexBuffer, buffer_sequence_size, deltaVertexBufferPos, posBytes, uint8_t, byteCount);
                }
            }

            // Write the vertex data out along with it's size in bytes.
            WRITE(buffer, buffer_sequence_size, position, deltaVertexBufferPos, int, 1);
            WRITE(buffer, buffer_sequence_size, position, deltaVertexBuffer[0], uint8_t, deltaVertexBufferPos);
        }

        free(deltaVertexBuffer);
    }

    if (header_in->has_retarget_data)
    {
        const int boneWeightTo11BitsMult = (1 << 11) - 1;
        const int smallBoneWeightTo10BitsMult = 2 * ((1 << 10) - 1); // For bone weights <= 0.5

        for (int i = 0; i < sequence_in->ssdr_frame_count; ++i)
        {
            // One-time joint info: Count, name, and hierarchy
            if (i == 0)
            {
                WRITE(buffer, buffer_size, position, sequence_in->retarget_data.bone_count, int, 1);

                for (int n = 0; n < sequence_in->retarget_data.bone_count; ++n)
                {
                    int stringSize = strlen(sequence_in->retarget_data.bone_names[n]);
                    WRITE(buffer, buffer_sequence_size, position, stringSize, int, 1);
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->retarget_data.bone_names[n][0], char, stringSize);
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->retarget_data.bone_parents[n], int, 1);
                }
            }

            for (int j = 0; j < sequence_in->retarget_data.bone_count; ++j)
            {
                bool posKeyFrame = true;
                bool rotKeyFrame = true;

                if (options != NULL && options->anim_keyframe_compression)
                {
                    uint8_t keyframe = sequence_in->retarget_data.keyframes[i][j];
                    WRITE(buffer, buffer_size, position, keyframe, uint8_t, 1);

                    posKeyFrame = (keyframe & kOMSKeyframePositionMask);
                    rotKeyFrame = (keyframe & kOMSKeyframeRotationMask);
                }

                if (posKeyFrame)
                {
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->retarget_data.bone_positions[i][j], oms_vec3_t, 1);
                }

                if (rotKeyFrame)
                {
                    WRITE(buffer, buffer_sequence_size, position, sequence_in->retarget_data.bone_rotations[i][j], oms_quaternion_t, 1);
                }
            }
        }

        // Rigging bone vert weights -- 4 indices + weights per vert
        for (int v = 0; v < sequence_in->vertex_count; ++v)
        {
            int boneIndices[4] = { (int)sequence_in->retarget_data.indices[v].x,
                                   (int)sequence_in->retarget_data.indices[v].y,
                                   (int)sequence_in->retarget_data.indices[v].z,
                                   (int)sequence_in->retarget_data.indices[v].w };

            uint8_t index01 = (uint8_t)(boneIndices[0] | (boneIndices[1] << 4));
            uint8_t index23 = (uint8_t)(boneIndices[2] | (boneIndices[3] << 4));

            WRITE(buffer, buffer_sequence_size, position, index01, uint8_t, 1);
            WRITE(buffer, buffer_sequence_size, position, index23, uint8_t, 1);

            float totalWeight = 0;
            for (int w = 0; w < 4; ++w)
            {
                totalWeight += sequence_in->retarget_data.weights[v].data[w];
            }

            int encodedWeights = (int)(sequence_in->retarget_data.weights[v].x / totalWeight * boneWeightTo11BitsMult)
                | (int)(sequence_in->retarget_data.weights[v].y / totalWeight * smallBoneWeightTo10BitsMult) << 11
                | (int)(sequence_in->retarget_data.weights[v].z / totalWeight * smallBoneWeightTo10BitsMult) << 21;

            WRITE(buffer, buffer_sequence_size, position, encodedWeights, int, 1);
        }
    }

    size_t positionOut = buffer_offset;
    size_t bufferSize = position;

    if (header_in->compression_level == OMS_COMPRESSION_NONE || header_in->compression_level == OMS_COMPRESSION_DELTA)
    {
        // Write out the correct sequence size.
        WRITE(buffer_out, buffer_size, positionOut, position, int, 1);
        WRITE(buffer_out, buffer_size, positionOut, buffer[0], uint8_t, position);
    }

    if (header_in->compression_level == OMS_COMPRESSION_GZIP)
    {
#if INCLUDE_GZIP
        z_stream defstream;
        defstream.zalloc = Z_NULL;
        defstream.zfree = Z_NULL;
        defstream.opaque = Z_NULL;

        defstream.avail_in = (uInt)position;
        defstream.next_in = (Bytef*)buffer;
        defstream.avail_out = (uInt)buffer_size;
        defstream.next_out = &buffer_out[positionOut + 4 + 10]; // Sequence size (4) + gzip header.

        deflateInit(&defstream, Z_BEST_COMPRESSION);
        deflate(&defstream, Z_FINISH);
        deflateEnd(&defstream);

        // Write the size before the data.
        int size_out = (int)defstream.total_out + 14;
        WRITE(buffer_out, buffer_size, positionOut, size_out, int, 1);

        // gzip header
        buffer_out[positionOut + 0] = 0x1f; // Magic header
        buffer_out[positionOut + 1] = 0x8b;
        buffer_out[positionOut + 2] = 0x08; // Compression method: deflate.
        buffer_out[positionOut + 3] = 0x00; // flags: file probably ascii text
        buffer_out[positionOut + 4] = 0x00; // Unix modification timestamp.
        buffer_out[positionOut + 5] = 0x00;
        buffer_out[positionOut + 6] = 0x00;
        buffer_out[positionOut + 7] = 0x00;
        buffer_out[positionOut + 8] = 0x00; // Extra Flags
        buffer_out[positionOut + 9] = 0x0b; // OS type: NTFS

        positionOut += 10;

        // gzip footer
        buffer_out[positionOut + defstream.total_out - 4] = 0x00; // CRC-32 Checksum
        buffer_out[positionOut + defstream.total_out - 3] = 0x00;
        buffer_out[positionOut + defstream.total_out - 2] = 0x00;
        buffer_out[positionOut + defstream.total_out - 1] = 0x00;

        int uncompressedSize = (int)position;
        memcpy(&buffer_out[positionOut + defstream.total_out], &uncompressedSize, 4);

        positionOut += defstream.total_out + 4;
#endif
    }

    if (header_in->compression_level == OMS_COMPRESSION_ZSTD)
    {
#if INCLUDE_ZSTD
        static ZSTD_CCtx* zstd_ctx = NULL;
        if (zstd_ctx == NULL)
        {
            zstd_ctx = ZSTD_createCCtx();
        }

        int compressionLevel = ZSTD_CLEVEL_DEFAULT;

        size_t const cBuffSize = ZSTD_compressBound(bufferSize);
        void* zstBuff = (void*)(&buffer_out[positionOut + 4]);
        size_t const cSize = ZSTD_compressCCtx(zstd_ctx, zstBuff, cBuffSize, &buffer[0], bufferSize, compressionLevel);

        // Write the size before the data.
        int size_out = (int)cSize;
        WRITE(buffer_out, buffer_size, positionOut, size_out, int, 1);
        positionOut += cSize;
#endif
    }

    free(buffer);
    return positionOut - buffer_offset;
}

oms_sequence_t* oms_alloc_sequence(int vertex_count,
    int normal_count,
    int uv_count,
    int index_count,
    int frame_count,
    int ssdr_bone_count,
    int retarget_bone_count)
{
    oms_sequence_t* sequence = (oms_sequence_t*)malloc(sizeof(oms_sequence_t));
    sequence->vertex_count = vertex_count;
    if (vertex_count > 0)
    {
        sequence->vertices = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * vertex_count);
    }
    else
    {
        sequence->vertices = NULL;
    }

    sequence->normal_count = normal_count;
    if (normal_count > 0)
    {
        sequence->normals = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * normal_count);
    }
    else
    {
        sequence->normals = NULL;
    }

    sequence->uv_count = uv_count;
    if (uv_count > 0)
    {
        sequence->uvs = (oms_vec2_t*)malloc(sizeof(oms_vec2_t) * uv_count);
    }
    else
    {
        sequence->uvs = NULL;
    }

    sequence->index_count = index_count;
    if (index_count > 0)
    {
        sequence->indices = malloc(oms_bytes_per_index(vertex_count) * index_count);
    }
    else
    {
        sequence->indices = NULL;
    }

    sequence->ssdr_frame_count = frame_count;
    sequence->ssdr_bone_count = ssdr_bone_count;
    sequence->extras.ssdr_weights_packed = NULL;
    if (frame_count > 1)
    {
        sequence->ssdr_bone_indices = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * vertex_count);
        sequence->ssdr_bone_weights = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * vertex_count);
        sequence->ssdr_frames = (oms_ssdr_frame_t*)malloc(sizeof(oms_ssdr_frame_t) * frame_count);
        for (int f = 0; f < frame_count; ++f)
        {
            sequence->ssdr_frames[f].matrices = (oms_matrix4x4_t*)malloc(sizeof(oms_matrix4x4_t) * ssdr_bone_count);
        }
    }
    else
    {
        sequence->ssdr_bone_indices = NULL;
        sequence->ssdr_bone_weights = NULL;
        sequence->ssdr_frames = NULL;
    }

    sequence->delta_frame_count = 0;
    sequence->delta_frames = NULL;

    oms_alloc_retarget_data(sequence, frame_count, retarget_bone_count);
    return sequence;
}

void oms_free_sequence(oms_sequence_t* sequence)
{
    if (sequence->vertex_count > 0)
    {
        free(sequence->vertices);
    }

    if (sequence->normal_count > 0)
    {
        free(sequence->normals);
    }

    if (sequence->uv_count > 0)
    {
        free(sequence->uvs);
    }

    if (sequence->index_count > 0)
    {
        free(sequence->indices);
    }

    if (sequence->ssdr_frame_count > 1)
    {
        free(sequence->ssdr_bone_indices);
        free(sequence->ssdr_bone_weights);
        if (sequence->extras.ssdr_weights_packed != NULL)
        {
            free(sequence->extras.ssdr_weights_packed);
        }

        // Skip if no frames have been allocated (Retarget w/o SSDR).
        if (sequence->ssdr_frames != NULL)
        {
            for (int i = 0; i < sequence->ssdr_frame_count; ++i)
            {
                free(sequence->ssdr_frames[i].matrices);
            }
        }

        free(sequence->ssdr_frames);
    }

    if (sequence->delta_frame_count > 0)
    {
        for (int i = 0; i < sequence->delta_frame_count; ++i)
        {
            free(sequence->delta_frames[i].vertices);
        }

        free(sequence->delta_frames);
    }

    oms_free_retarget_data(sequence);
}

void oms_alloc_retarget_data(oms_sequence_t* sequence, int frame_count, int num_bones)
{
    sequence->retarget_data.bone_count = num_bones;
    if (num_bones > 0)
    {
        //[Current anim framecount is stored as ssdr_frame_count]
        sequence->retarget_data.indices = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * sequence->vertex_count);
        sequence->retarget_data.weights = (oms_vec4_t*)malloc(sizeof(oms_vec4_t) * sequence->vertex_count);

        sequence->retarget_data.keyframes = (uint8_t**)malloc(sizeof(uint8_t*) * frame_count);
        sequence->retarget_data.bone_positions = (oms_vec3_t**)malloc(sizeof(oms_vec3_t*) * frame_count);
        sequence->retarget_data.bone_rotations = (oms_quaternion_t**)malloc(sizeof(oms_quaternion_t*) * frame_count);

        sequence->retarget_data.bone_names = (char**)malloc(sizeof(char*) * num_bones);
        memset(sequence->retarget_data.bone_names, 0, (sizeof(char*) * num_bones));
        sequence->retarget_data.bone_parents = (int*)malloc(sizeof(int) * num_bones);

        for (int f = 0; f < frame_count; ++f)
        {
            sequence->retarget_data.keyframes[f] = (uint8_t*)malloc(sizeof(uint8_t) * num_bones);
            sequence->retarget_data.bone_positions[f] = (oms_vec3_t*)malloc(sizeof(oms_vec3_t) * num_bones);
            sequence->retarget_data.bone_rotations[f] = (oms_quaternion_t*)malloc(sizeof(oms_quaternion_t) * num_bones);
        }
    }
    else
    {
        sequence->retarget_data.weights = NULL;
        sequence->retarget_data.indices = NULL;
        sequence->retarget_data.bone_names = NULL;
        sequence->retarget_data.bone_parents = NULL;
        sequence->retarget_data.keyframes = NULL;
        sequence->retarget_data.bone_positions = NULL;
        sequence->retarget_data.bone_rotations = NULL;
    }
}

void oms_free_retarget_data(oms_sequence_t* sequence)
{
    if (sequence->retarget_data.bone_count <= 0)
    {
        return;
    }
    free(sequence->retarget_data.bone_parents);
    free(sequence->retarget_data.indices);
    free(sequence->retarget_data.weights);

    for (int b = 0; b < sequence->retarget_data.bone_count; ++b)
    {
        if (sequence->retarget_data.bone_names[b] != NULL)
        {
            free(sequence->retarget_data.bone_names[b]);
        }
    }
    free(sequence->retarget_data.bone_names);

    for (int f = 0; f < sequence->ssdr_frame_count; ++f)
    {
        if (sequence->retarget_data.keyframes != NULL)
        {
            free(sequence->retarget_data.keyframes[f]);
        }
        free(sequence->retarget_data.bone_positions[f]);
        free(sequence->retarget_data.bone_rotations[f]);
    }

    if (sequence->retarget_data.keyframes != NULL)
    {
        free(sequence->retarget_data.keyframes);
    }
    free(sequence->retarget_data.bone_positions);
    free(sequence->retarget_data.bone_rotations);
}

void oms_set_retarget_bone_name(oms_sequence_t* sequence, int bone, char* name)
{
    if (sequence->retarget_data.bone_names[bone] != NULL)
    {
        free(sequence->retarget_data.bone_names[bone]);
    }
    int len = strlen(name) + 1;
    sequence->retarget_data.bone_names[bone] = (char*)malloc(sizeof(char) * len);
    memcpy(sequence->retarget_data.bone_names[bone], name, len);
}

void oms_sequence_compute_aabb(oms_sequence_t* sequence)
{
    oms_aabb_t* result = &sequence->aabb;
    result->min.x = FLT_MAX;
    result->min.y = FLT_MAX;
    result->min.z = FLT_MAX;

    result->max.x = -FLT_MAX;
    result->max.y = -FLT_MAX;
    result->max.z = -FLT_MAX;

    for (int i = 0; i < sequence->vertex_count; i++)
    {
        oms_vec3_t* vertex = &sequence->vertices[i];

        result->min.x = vertex->x < result->min.x ? vertex->x : result->min.x;
        result->min.y = vertex->y < result->min.y ? vertex->y : result->min.y;
        result->min.z = vertex->z < result->min.z ? vertex->z : result->min.z;

        result->max.x = vertex->x > result->max.x ? vertex->x : result->max.x;
        result->max.y = vertex->y > result->max.y ? vertex->y : result->max.y;
        result->max.z = vertex->z > result->max.z ? vertex->z : result->max.z;
    }
}

void rot_matrix_to_quaternion(oms_matrix4x4_t* mat, oms_quaternion_t* result)
{
    float t = mat->m[0] + mat->m[5] + mat->m[10];

    // we protect the division by s by ensuring that s>=1
    if (t >= 0)
    { // by w
        float s = sqrt(t + 1.0f);
        result->w = 0.5f * s;
        s = 0.5f / s;
        result->x = (mat->m[9] - mat->m[6]) * s;
        result->y = (mat->m[2] - mat->m[8]) * s;
        result->z = (mat->m[4] - mat->m[1]) * s;
    }
    else if ((mat->m[0] > mat->m[5]) && (mat->m[0] > mat->m[10]))
    { // by x
        float s = sqrt(1.0f + mat->m[0] - mat->m[5] - mat->m[10]);
        result->x = s * 0.5f;
        s = 0.5f / s;
        result->y = (mat->m[4] + mat->m[1]) * s;
        result->z = (mat->m[2] + mat->m[8]) * s;
        result->w = (mat->m[9] - mat->m[6]) * s;
    }
    else if (mat->m[5] > mat->m[10])
    { // by y
        float s = sqrt(1.0f + mat->m[5] - mat->m[0] - mat->m[10]);
        result->y = s * 0.5f;
        s = 0.5f / s;
        result->x = (mat->m[4] + mat->m[1]) * s;
        result->z = (mat->m[9] + mat->m[6]) * s;
        result->w = (mat->m[2] - mat->m[8]) * s;
    }
    else
    { // by z
        float s = sqrt(1.0f + mat->m[10] - mat->m[0] - mat->m[5]);
        result->z = s * 0.5f;
        s = 0.5f / s;
        result->x = (mat->m[2] + mat->m[8]) * s;
        result->y = (mat->m[9] + mat->m[6]) * s;
        result->w = (mat->m[4] - mat->m[1]) * s;
    }
}

void quaternion_to_rot_matrix(oms_quaternion_t* quat, oms_matrix4x4_t* result)
{
    float q0 = quat->w;
    float q1 = quat->x;
    float q2 = quat->y;
    float q3 = quat->z;

    memset(result->m, 0, sizeof(oms_matrix4x4_t));

    result->m[0] = 2.0f * (q0 * q0 + q1 * q1) - 1.0f;
    result->m[1] = 2.0f * (q1 * q2 - q0 * q3);
    result->m[2] = 2.0f * (q1 * q3 + q0 * q2);

    result->m[4] = 2.0f * (q1 * q2 + q0 * q3);
    result->m[5] = 2.0f * (q0 * q0 + q2 * q2) - 1.0f;
    result->m[6] = 2.0f * (q2 * q3 - q0 * q1);

    result->m[8] = 2.0f * (q1 * q3 - q0 * q2);
    result->m[9] = 2.0f * (q2 * q3 + q0 * q1);
    result->m[10] = 2.0f * (q0 * q0 + q3 * q3) - 1.0f;

    result->m[15] = 1.0f;
}

// Mirror on x-axis for engines that require it.
void oms_mirror_sequence_x(oms_sequence_t* sequence)
{
    // Mirror vertices and normals across x axis;
    for (int i = 0; i < sequence->vertex_count; ++i)
    {
        sequence->vertices[i].x = -sequence->vertices[i].x;
    }

    for (int i = 0; i < sequence->normal_count; ++i)
    {
        sequence->normals[i].x = -sequence->normals[i].x;
    }

    // Invert triangle winding order due to axis flip.
    if (sequence->vertex_count <= (UINT16_MAX + 1))
    {
        uint16_t* indices = (uint16_t*)sequence->indices;
        uint16_t tmp;
        for (int i = 0; i < (sequence->index_count / 3); ++i)
        {
            tmp = indices[(i * 3) + 2];
            indices[(i * 3) + 2] = indices[(i * 3) + 1];
            indices[(i * 3) + 1] = tmp;
        }
    }
    else
    {
        uint32_t* indices = (uint32_t*)sequence->indices;
        uint32_t tmp;
        for (int i = 0; i < (sequence->index_count / 3); ++i)
        {
            tmp = indices[(i * 3) + 2];
            indices[(i * 3) + 2] = indices[(i * 3) + 1];
            indices[(i * 3) + 1] = tmp;
        }
    }

    // If this isn't a single frame sequence mirror all frames of SSDR and retarget.
    if (sequence->ssdr_frame_count > 1)
    {
        oms_quaternion_t rot;

        for (int i = 0; i < sequence->ssdr_frame_count; ++i)
        {
            // Mirror SSDR.
            for (int b = 0; b < sequence->ssdr_bone_count; ++b)
            {
                // Convert rotation to quaternion, then mirror the quaternion over the x axis.
                rot_matrix_to_quaternion(&sequence->ssdr_frames[i].matrices[b], &rot);
                rot.y = -rot.y;
                rot.z = -rot.z;

                // Store position from transformation matrix.
                oms_vec3_t pos = {{ sequence->ssdr_frames[i].matrices[b].m[12], sequence->ssdr_frames[i].matrices[b].m[13], sequence->ssdr_frames[i].matrices[b].m[14] }};

                // Convert mirrored quaternion back to rotation matrix.
                quaternion_to_rot_matrix(&rot, &sequence->ssdr_frames[i].matrices[b]);

                // Restore and mirror translation.
                sequence->ssdr_frames[i].matrices[b].m[12] = -pos.x;
                sequence->ssdr_frames[i].matrices[b].m[13] = pos.y;
                sequence->ssdr_frames[i].matrices[b].m[14] = pos.z;
            }

            // Mirror retarget data.
            if (sequence->retarget_data.bone_count > 0)
            {
                for (int b = 0; b < sequence->retarget_data.bone_count; ++b)
                {
                    // Mirror positions
                    sequence->retarget_data.bone_positions[i][b].x = -sequence->retarget_data.bone_positions[i][b].x;

                    // Mirror rotations
                    sequence->retarget_data.bone_rotations[i][b].y = -sequence->retarget_data.bone_rotations[i][b].y;
                    sequence->retarget_data.bone_rotations[i][b].z = -sequence->retarget_data.bone_rotations[i][b].z;
                }
            }
        }
    }
    else
    {
        // Mirror retarget data.
        if (sequence->retarget_data.bone_count > 0)
        {
            for (int b = 0; b < sequence->retarget_data.bone_count; ++b)
            {
                // Mirror position
                sequence->retarget_data.bone_positions[0][b].x = -sequence->retarget_data.bone_positions[0][b].x;

                // Mirror rotation
                sequence->retarget_data.bone_rotations[0][b].y = -sequence->retarget_data.bone_rotations[0][b].y;
                sequence->retarget_data.bone_rotations[0][b].z = -sequence->retarget_data.bone_rotations[0][b].z;
            }
        }
    }
}
