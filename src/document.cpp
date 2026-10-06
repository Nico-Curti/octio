#include "octio/document.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace octio {

  /// Construct a document around an already-created backend.
  Document::Document(std::unique_ptr<Backend> backend) : backend_(std::move(backend)) {}

  /// Destroy the document and backend.
  Document::~Document() = default;

  /// Move-construct a document.
  Document::Document(Document && ) noexcept = default;

  /// Move-assign a document.
  Document& Document::operator = (Document && ) noexcept = default;

  /// Open an FDA or E2E file using extension and magic detection.
  std::unique_ptr<Document> Document::open(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
      throw std::runtime_error("Cannot open file: " + path);
    }
    char magic[21]{};
    f.read(magic, sizeof(magic));
    const std::string head(magic, static_cast<std::size_t>(f.gcount()));
    const auto ext = std::filesystem::path(path).extension().string();
    if (head.rfind("FOCTFDA", 0) == 0 || ext == ".fda" || ext == ".FDA") {
      return std::unique_ptr<Document>(new Document(make_fda_backend(path)));
    }
    if (head.rfind("E2EMultipleVolumeFile", 0) == 0 || ext == ".e2e" || ext == ".E2E") {
      return std::unique_ptr<Document>(new Document(make_e2e_backend(path)));
    }
    // Single-volume E2E does not begin with E2EMultipleVolumeFile. Try E2E parser first.
    try {
      return std::unique_ptr<Document>(new Document(make_e2e_backend(path)));
    } catch (...) {
      throw std::runtime_error("Unsupported or unrecognized OCT file: " + path);
    }
  }

  /// Return the detected native file format.
  Format Document::format() const noexcept {
    return backend_->format();
  }
  /// Return the original source path.
  const std::string& Document::path() const noexcept {
    return backend_->path();
  }
  /// Return decoded patient metadata.
  const PatientInfo& Document::patient() const noexcept {
    return backend_->patient();
  }
  /// Return decoded acquisition metadata.
  const AcquisitionInfo& Document::acquisition() const noexcept {
    return backend_->acquisition();
  }
  /// Return decoded device metadata.
  const DeviceInfo& Document::device() const noexcept {
    return backend_->device();
  }
  /// Return backend-specific scalar metadata as strings.
  const std::unordered_map<std::string, std::string>& Document::metadata() const noexcept {
    return backend_->metadata();
  }
  /// Return every native chunk/data element known to the parser.
  const std::vector<ChunkInfo>& Document::chunks() const noexcept {
    return backend_->chunks();
  }
  /// Return OCT volumes.
  const std::vector<VolumeView>& Document::volumes() const noexcept {
    return backend_->volumes();
  }
  /// Return fundus/localizer images.
  const std::vector<FundusView>& Document::fundus_images() const noexcept {
    return backend_->fundus_images();
  }
  /// Return all layer segmentations.
  const std::vector<SegmentationView>& Document::segmentations() const noexcept {
    return backend_->segmentations();
  }
  /// Return a zero-copy raw view of a native chunk payload.
  ArrayView1D Document::chunk_bytes(std::size_t i) const {
    return backend_->chunk_bytes(i);
  }
  /// Replace a native chunk payload in memory.
  void Document::replace_chunk(std::size_t i, const std::vector<std::uint8_t>& b) {
    backend_->replace_chunk(i, b);
  }
  /// Update patient fields.
  void Document::set_patient(const PatientInfo& p) {
    backend_->set_patient(p);
  }
  /// Replace a raw Topcon volume.
  void Document::set_fda_raw_volume(
    std::size_t i,
    const std::uint16_t* p,
    std::size_t s,
    std::size_t h,
    std::size_t w
  ) {
    backend_->set_fda_raw_volume(i, p, s, h, w);
  }
  /// Replace or append a Topcon segmentation.
  void Document::set_fda_segmentation(
    const std::string& l,
    const std::uint16_t* p,
    std::size_t h,
    std::size_t w
  ) {
    backend_->set_fda_segmentation(l, p, h, w);
  }
  /// Replace or append a Topcon JPEG fundus image.
  void Document::set_fda_fundus_jpeg(
    const std::uint8_t* p,
    std::size_t n,
    std::size_t h,
    std::size_t w,
    std::size_t c
  ) {
    backend_->set_fda_fundus_jpeg(p, n, h, w, c);
  }
  /// Save a new native file while preserving unknown data.
  void Document::save(const std::string& p) const {
    backend_->save(p);
  }
  /// Copy one A-scan into float32 output.
  void Document::copy_a_scan_f32(
    std::size_t vi,
    std::size_t si,
    std::size_t xi,
    float* o,
    std::size_t n,
    bool d
  ) const {
    backend_->copy_a_scan_f32(vi, si, xi, o, n, d);
  }
  /// Copy one B-scan into float32 output.
  void Document::copy_b_scan_f32(
    std::size_t vi,
    std::size_t si,
    float* o,
    std::size_t n,
    bool d
  ) const {
    backend_->copy_b_scan_f32(vi, si, o, n, d);
  }

  /// Convert a Heidelberg unsigned 16-bit custom float code to float32.
  float e2e_ufloat16_to_float(std::uint16_t value) noexcept {
    const std::uint16_t mantissa = value & 0x03ffu;
    const std::uint16_t exponent = (value >> 10u) & 0x003fu;
    return (1.0f + static_cast<float>(mantissa) / 1024.0f) *
    std::ldexp(1.0f, static_cast<int>(exponent) - 63);
  }

  /// Apply the common Heidelberg OCT display-intensity transform.
  float e2e_display_transform(float value) noexcept {
    if (!std::isfinite(value) || value >= std::numeric_limits<float>::max()) return 0.0f;
    float out = value;
    if (out <= 1.0f) {
      out = (std::log(out + 2.44e-4f) + 8.3f) / 8.285f;
    }
    return std::clamp(out, 0.0f, 1.0f);
  }

}
// namespace octio
