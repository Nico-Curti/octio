#pragma once

#include <stddef.h>
#include <stdint.h>

#include "octio/export.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct octio_handle octio_handle;

typedef struct octio_array2d_info {
  const void* data;
  size_t height;
  size_t width;
  ptrdiff_t row_stride_bytes;
  int dtype;
} octio_array2d_info;

typedef struct octio_fundus_info {
  const void* data;
  size_t byte_size;
  size_t height;
  size_t width;
  size_t channels;
  int dtype;
  int encoding;
} octio_fundus_info;

typedef struct octio_segmentation_info {
  const void* data;
  size_t height;
  size_t width;
  ptrdiff_t row_stride_bytes;
  int dtype;
  int layer_id;
  size_t slice_index;
  int measured_from_bottom;
} octio_segmentation_info;

/** \brief Open an OCT file and return an opaque handle, or NULL on failure. */
OCTIO_API octio_handle* octio_open(const char* path);

/** \brief Close a handle returned by octio_open. */
OCTIO_API void octio_close(octio_handle* handle);

/** \brief Return the last thread-local error string. */
OCTIO_API const char* octio_last_error(void);

/** \brief Return the native format code: 1=FDA, 2=E2E. */
OCTIO_API int octio_format(const octio_handle* handle);

/** \brief Return metadata as UTF-8 JSON in an internal stable buffer. */
OCTIO_API const char* octio_metadata_json(octio_handle* handle);

/** \brief Return native chunk descriptors as UTF-8 JSON. */
OCTIO_API const char* octio_chunks_json(octio_handle* handle);

/** \brief Return the number of OCT volumes. */
OCTIO_API size_t octio_volume_count(const octio_handle* handle);

/** \brief Return a stable UTF-8 volume identifier. */
OCTIO_API const char* octio_volume_id(octio_handle* handle, size_t volume_index);

/** \brief Return non-zero when a volume stores Heidelberg custom ufloat16 codes. */
OCTIO_API int octio_volume_is_e2e_custom_float(const octio_handle* handle, size_t volume_index);

/** \brief Return the number of B-scans in a volume. */
OCTIO_API size_t octio_volume_slice_count(const octio_handle* handle, size_t volume_index);

/** \brief Return a zero-copy B-scan view. */
OCTIO_API int octio_volume_slice(const octio_handle* handle, size_t volume_index,
                size_t slice_index, octio_array2d_info* out);

/** \brief Return an encoded B-scan payload when the native volume is JPEG-compressed. */
OCTIO_API int octio_volume_slice_encoded(const octio_handle* handle, size_t volume_index,
                    size_t slice_index, const void** data,
                    size_t* size, size_t* height, size_t* width,
                    int* encoding);

/** \brief Copy one B-scan to float32. */
OCTIO_API int octio_copy_bscan_f32(const octio_handle* handle, size_t volume_index,
                  size_t slice_index, float* out, size_t count,
                  int decode_e2e_intensity);

/** \brief Copy one A-scan to float32. */
OCTIO_API int octio_copy_ascan_f32(const octio_handle* handle, size_t volume_index,
                  size_t slice_index, size_t x_index,
                  float* out, size_t count,
                  int decode_e2e_intensity);

/** \brief Return the number of fundus/localizer images. */
OCTIO_API size_t octio_fundus_count(const octio_handle* handle);

/** \brief Return a fundus/localizer data view. */
OCTIO_API int octio_fundus(const octio_handle* handle, size_t index, octio_fundus_info* out);

/** \brief Return the number of segmentation arrays. */
OCTIO_API size_t octio_segmentation_count(const octio_handle* handle);

/** \brief Return one segmentation view. */
OCTIO_API int octio_segmentation(const octio_handle* handle, size_t index,
                octio_segmentation_info* out);

/** \brief Return a segmentation layer name in an internal UTF-8 buffer. */
OCTIO_API const char* octio_segmentation_name(octio_handle* handle, size_t index);

/** \brief Return a raw native chunk as a zero-copy byte span. */
OCTIO_API int octio_chunk_bytes(const octio_handle* handle, size_t index,
                const void** data, size_t* size);

/** \brief Replace a native chunk payload. */
OCTIO_API int octio_replace_chunk(octio_handle* handle, size_t index,
                  const void* data, size_t size);

/** \brief Update common patient fields. */
OCTIO_API int octio_set_patient(octio_handle* handle,
                const char* patient_id,
                const char* first_name,
                const char* middle_name,
                const char* last_name,
                const char* sex,
                const char* birth_date);

/** \brief Replace an FDA raw uint16 OCT volume. */
OCTIO_API int octio_set_fda_raw_volume(octio_handle* handle, size_t volume_index,
                    const uint16_t* data, size_t slices,
                    size_t height, size_t width);

/** \brief Replace or append an FDA segmentation layer. */
OCTIO_API int octio_set_fda_segmentation(octio_handle* handle, const char* layer,
                    const uint16_t* data, size_t height, size_t width);

/** \brief Replace or append an FDA JPEG fundus image. */
OCTIO_API int octio_set_fda_fundus_jpeg(octio_handle* handle, const uint8_t* jpeg,
                    size_t size, size_t height, size_t width,
                    size_t channels);

/** \brief Save the document to a new file. */
OCTIO_API int octio_save(const octio_handle* handle, const char* output_path);

#ifdef __cplusplus
}
#endif
