#pragma once

#include "octio/export.hpp"
#include "octio/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace octio {

class Backend;

/**
 * \brief High-level, format-independent OCT file interface.
 *
 * The class owns a memory mapping and a backend-specific parser. Views returned
 * by this class remain valid until the Document is destroyed or re-opened.
 */
class Document {
public:
  /** \brief Open an FDA or E2E file using extension and magic detection. */
  static OCTIO_API std::unique_ptr<Document> open(const std::string& path);

  /** \brief Destroy the document and release the memory mapping. */
  OCTIO_API ~Document();

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  OCTIO_API Document(Document&&) noexcept;
  OCTIO_API Document& operator=(Document&&) noexcept;

  /** \brief Return the detected native file format. */
  OCTIO_API Format format() const noexcept;

  /** \brief Return the original source path. */
  OCTIO_API const std::string& path() const noexcept;

  /** \brief Return decoded patient metadata. */
  OCTIO_API const PatientInfo& patient() const noexcept;

  /** \brief Return decoded acquisition metadata. */
  OCTIO_API const AcquisitionInfo& acquisition() const noexcept;

  /** \brief Return decoded device metadata. */
  OCTIO_API const DeviceInfo& device() const noexcept;

  /** \brief Return backend-specific scalar metadata as strings. */
  OCTIO_API const std::unordered_map<std::string, std::string>&
  metadata() const noexcept;

  /** \brief Return every native chunk/data element known to the parser. */
  OCTIO_API const std::vector<ChunkInfo>& chunks() const noexcept;

  /** \brief Return OCT volumes. Pixel views are zero-copy where the format allows it. */
  OCTIO_API const std::vector<VolumeView>& volumes() const noexcept;

  /** \brief Return fundus/localizer images, encoded or raw. */
  OCTIO_API const std::vector<FundusView>& fundus_images() const noexcept;

  /** \brief Return all layer segmentations found in the source file. */
  OCTIO_API const std::vector<SegmentationView>& segmentations() const noexcept;

  /** \brief Return a zero-copy raw view of a native chunk payload. */
  OCTIO_API ArrayView1D chunk_bytes(std::size_t chunk_index) const;

  /**
   * \brief Replace a native chunk payload in memory.
   *
   * FDA permits resized payloads because its outer chunk stream is length
   * prefixed. E2E replacement is restricted to the original byte count to
   * avoid invalidating directory offsets.
   */
  OCTIO_API void replace_chunk(
    std::size_t chunk_index,
    const std::vector<std::uint8_t>& bytes
  );

  /**
   * \brief Update patient fields when the backend supports deterministic writing.
   *
   * FDA PATIENT_INFO_03 fields are re-obfuscated with the inferred fixed XOR
   * keystream. E2E uses its fixed-size patient record and rejects values that
   * do not fit the native field widths.
   */
  OCTIO_API void set_patient(const PatientInfo& patient);

  /**
   * \brief Replace a raw Topcon IMG_MOT_COMP_03 volume.
   *
   * The operation is FDA-only. The source chunk's opaque trailer is preserved.
   * Width, height and number of slices can change, but callers must update any
   * dependent registration/segmentation metadata themselves when geometry changes.
   */
  OCTIO_API void set_fda_raw_volume(std::size_t volume_index,
              const std::uint16_t* data,
              std::size_t slices,
              std::size_t height,
              std::size_t width);

  /**
   * \brief Replace or append a Topcon layer segmentation map.
   *
   * Values are stored in the native uint16 representation expected by
   * CONTOUR_INFO. Existing opaque trailers are preserved when replacing a layer.
   */
  OCTIO_API void set_fda_segmentation(const std::string& layer,
                const std::uint16_t* data,
                std::size_t height,
                std::size_t width);

  /**
   * \brief Replace or append a Topcon JPEG fundus/localizer payload.
   *
   * JPEG encoding is intentionally delegated to callers so the core library
   * has no mandatory codec dependency.
   */
  OCTIO_API void set_fda_fundus_jpeg(const std::uint8_t* jpeg,
              std::size_t size,
              std::size_t height,
              std::size_t width,
              std::size_t channels = 3);

  /**
   * \brief Save a new native file while preserving unknown data.
   *
   * FDA files are rebuilt losslessly from the indexed chunk stream. E2E files
   * are copied byte-for-byte and patched only at same-size modified regions.
   */
  OCTIO_API void save(const std::string& output_path) const;

  /** \brief Copy one A-scan (depth profile) from a volume into float32 output. */
  OCTIO_API void copy_a_scan_f32(std::size_t volume_index,
            std::size_t slice_index,
            std::size_t x_index,
            float* output,
            std::size_t output_count,
            bool decode_e2e_intensity = true) const;

  /** \brief Copy one B-scan to float32, optionally decoding Heidelberg ufloat16 intensity. */
  OCTIO_API void copy_b_scan_f32(std::size_t volume_index,
            std::size_t slice_index,
            float* output,
            std::size_t output_count,
            bool decode_e2e_intensity = true) const;

private:
  explicit Document(std::unique_ptr<Backend> backend);
  std::unique_ptr<Backend> backend_;
};

/** \brief Convert a Heidelberg unsigned 16-bit custom float code to float32. */
OCTIO_API float e2e_ufloat16_to_float(std::uint16_t value) noexcept;

/** \brief Apply the common Heidelberg OCT display-intensity transform. */
OCTIO_API float e2e_display_transform(float value) noexcept;

}  // namespace octio
