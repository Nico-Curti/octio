#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace octio {

/** \brief Supported native OCT container formats. */
enum class Format : std::uint8_t { Unknown = 0, FDA = 1, E2E = 2 };

/** \brief Scalar element types exposed by zero-copy array views. */
enum class DType : std::uint8_t { UInt8 = 1, UInt16 = 2, Float32 = 3 };

/** \brief Encoding used by an image payload. */
enum class ImageEncoding : std::uint8_t { Raw = 0, JPEG = 1, Unknown = 255 };

/** \brief Patient-level metadata shared by all backends. */
struct PatientInfo {
  std::string patient_id;
  std::string first_name;
  std::string middle_name;
  std::string last_name;
  std::string sex;
  std::string birth_date;
  std::string physician;
  std::string zip_code;
  std::string address;
  std::string phones;
  std::string description;
};

/** \brief Acquisition metadata shared by all backends. */
struct AcquisitionInfo {
  std::string datetime;
  std::string laterality;
  std::int64_t session_id{0};
  std::int32_t scan_mode{-1};
  std::string label;
};

/** \brief Device metadata shared by all backends. */
struct DeviceInfo {
  std::string model;
  std::string serial;
  std::string spectrometer_serial;
  std::string rom_version;
  std::string software_name;
  std::string software_version;
};

/** \brief Raw chunk descriptor for lossless low-level access. */
struct ChunkInfo {
  std::size_t index{0};
  std::string name;
  std::uint64_t payload_offset{0};
  std::uint64_t payload_size{0};
  std::uint32_t type{0};
  std::int32_t slice_id{0};
  std::int16_t indicator{0};
  std::uint32_t patient_db_id{0};
  std::uint32_t study_id{0};
  std::uint32_t series_id{0};
};

/** \brief Non-owning two-dimensional array view into a mapped file. */
struct ArrayView2D {
  const void* data{nullptr};
  std::size_t height{0};
  std::size_t width{0};
  std::ptrdiff_t row_stride_bytes{0};
  DType dtype{DType::UInt8};
};

/** \brief Non-owning one-dimensional array view into a mapped file. */
struct ArrayView1D {
  const void* data{nullptr};
  std::size_t length{0};
  std::ptrdiff_t stride_bytes{0};
  DType dtype{DType::UInt8};
};

/** \brief Single OCT B-scan view. */
struct BScanView {
  ArrayView2D pixels;
  std::size_t slice_index{0};
  ImageEncoding encoding{ImageEncoding::Raw};
  const void* encoded_data{nullptr};
  std::size_t encoded_size{0};
  std::size_t encoded_height{0};
  std::size_t encoded_width{0};
};

/** \brief Logical OCT volume with zero-copy B-scan access. */
struct VolumeView {
  std::string id;
  std::vector<BScanView> slices;
  double spacing_x_mm{0.0};
  double spacing_y_mm{0.0};
  double spacing_z_mm{0.0};
  bool e2e_custom_float{false};
};

/** \brief Fundus/localizer image view or encoded image buffer. */
struct FundusView {
  std::string id;
  ImageEncoding encoding{ImageEncoding::Unknown};
  const void* data{nullptr};
  std::size_t byte_size{0};
  std::size_t height{0};
  std::size_t width{0};
  std::size_t channels{1};
  DType dtype{DType::UInt8};
};

/** \brief Retinal layer boundary or segmentation map. */
struct SegmentationView {
  std::string layer;
  std::int32_t layer_id{-1};
  std::size_t slice_index{0};
  ArrayView2D values;
  bool measured_from_bottom{false};
};

}  // namespace octio
