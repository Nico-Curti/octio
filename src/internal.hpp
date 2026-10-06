#pragma once

#include "octio/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace octio {

class MappedFile {
public:
  /// Open and memory-map a file read-only.
  explicit MappedFile(const std::string& path);
  /// Release the mapping and native file handles.
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&&) noexcept;
  MappedFile& operator=(MappedFile&&) noexcept;
  /// Return the first mapped byte.
  const std::uint8_t* data() const noexcept { return data_; }
  /// Return mapped byte count.
  std::size_t size() const noexcept { return size_; }
  /// Return original path.
  const std::string& path() const noexcept { return path_; }
private:
  std::string path_;
  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
#if defined(_WIN32)
  void* file_{nullptr};
  void* mapping_{nullptr};
#else
  int fd_{-1};
#endif
};

struct Replacement {
  std::vector<std::uint8_t> bytes;
};

class Backend {
public:
  /// Virtual backend destructor.
  virtual ~Backend() = default;
  /// Return the native format.
  virtual Format format() const noexcept = 0;
  /// Return source path.
  virtual const std::string& path() const noexcept = 0;
  /// Return patient metadata.
  virtual const PatientInfo& patient() const noexcept = 0;
  /// Return acquisition metadata.
  virtual const AcquisitionInfo& acquisition() const noexcept = 0;
  /// Return device metadata.
  virtual const DeviceInfo& device() const noexcept = 0;
  /// Return scalar metadata map.
  virtual const std::unordered_map<std::string, std::string>& metadata() const noexcept = 0;
  /// Return native chunks/data elements.
  virtual const std::vector<ChunkInfo>& chunks() const noexcept = 0;
  /// Return OCT volumes.
  virtual const std::vector<VolumeView>& volumes() const noexcept = 0;
  /// Return fundus/localizer views.
  virtual const std::vector<FundusView>& fundus_images() const noexcept = 0;
  /// Return segmentation views.
  virtual const std::vector<SegmentationView>& segmentations() const noexcept = 0;
  /// Return a raw chunk payload view.
  virtual ArrayView1D chunk_bytes(std::size_t index) const = 0;
  /// Replace a raw native payload.
  virtual void replace_chunk(std::size_t index, const std::vector<std::uint8_t>& bytes) = 0;
  /// Update patient metadata.
  virtual void set_patient(const PatientInfo& patient) = 0;
  /// Replace a Topcon raw volume or reject for unsupported backends.
  virtual void set_fda_raw_volume(std::size_t volume_index, const std::uint16_t* data,
                  std::size_t slices, std::size_t height,
                  std::size_t width) = 0;
  /// Replace a Topcon segmentation or reject for unsupported backends.
  virtual void set_fda_segmentation(const std::string& layer, const std::uint16_t* data,
                    std::size_t height, std::size_t width) = 0;
  /// Replace a Topcon JPEG fundus image or reject for unsupported backends.
  virtual void set_fda_fundus_jpeg(const std::uint8_t* jpeg, std::size_t size,
                  std::size_t height, std::size_t width,
                  std::size_t channels) = 0;
  /// Save a new native file.
  virtual void save(const std::string& output_path) const = 0;
  /// Copy an A-scan into float32 output.
  virtual void copy_a_scan_f32(std::size_t volume_index, std::size_t slice_index,
                std::size_t x_index, float* output,
                std::size_t output_count, bool decode_e2e) const = 0;
  /// Copy a B-scan into float32 output.
  virtual void copy_b_scan_f32(std::size_t volume_index, std::size_t slice_index,
                float* output, std::size_t output_count,
                bool decode_e2e) const = 0;
};

/// Read a little-endian unsigned integer from mapped bytes.
/// Read a little-endian scalar from an unaligned byte buffer.
template <typename T>
T read_le(const std::uint8_t* p) {
  static_assert(std::is_unsigned<T>::value, "read_le requires an unsigned type");
  T v = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    v |= static_cast<T>(p[i]) << (8u * i);
  }
  return v;
}

/// Write a little-endian unsigned integer to a byte vector.
/// Write a little-endian scalar to an unaligned byte buffer.
template <typename T>
void write_le(std::uint8_t* p, T v) {
  static_assert(std::is_unsigned<T>::value, "write_le requires an unsigned type");
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    p[i] = static_cast<std::uint8_t>((v >> (8u * i)) & 0xffu);
  }
}

/// Read a little-endian float32 without alignment assumptions.
float read_f32_le(const std::uint8_t* p) noexcept;
/// Read a little-endian float64 without alignment assumptions.
double read_f64_le(const std::uint8_t* p) noexcept;
/// Write a little-endian float32 without alignment assumptions.
void write_f32_le(std::uint8_t* p, float v) noexcept;
/// Convert a fixed-width C-style byte string to trimmed std::string.
std::string fixed_string(const std::uint8_t* p, std::size_t n);
/// Escape a UTF-8/ASCII string for JSON output.
std::string json_escape(const std::string& s);
/// Validate an in-range mapped span and return its start pointer.
const std::uint8_t* checked_span(const MappedFile& file, std::uint64_t offset, std::uint64_t size);

std::unique_ptr<Backend> make_fda_backend(const std::string& path);
std::unique_ptr<Backend> make_e2e_backend(const std::string& path);

}  // namespace octio
