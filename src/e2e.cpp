#include "internal.hpp"
#include "octio/document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace octio {
  namespace {

    constexpr std::uint32_t E2E_IMAGE_TYPE = 1073741824u;
    constexpr std::uint32_t E2E_CONTOUR_V2 = 0x2723u;
    constexpr std::uint32_t E2E_CONTOUR_V1 = 10019u;

    /// Decode a Latin-1 fixed-width string into UTF-8 for the common 0..255 range.
    std::string latin1_to_utf8(const std::uint8_t* p, std::size_t n) {
      std::string out;
      for (std::size_t i = 0; i < n && p[i] != 0; ++i) {
        const unsigned c = p[i];
        if (c < 0x80) {
          out.push_back(static_cast<char>(c));
        } else {
          out.push_back(static_cast<char>(0xc0 | (c >> 6)));
          out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
      }
      return out;
    }

    /// Encode basic UTF-8/Latin-1 text into one-byte Heidelberg fields.
    std::vector<std::uint8_t> utf8_to_latin1_fixed(const std::string& s, std::size_t n) {
      std::vector<std::uint8_t> out(n, 0);
      std::size_t oi = 0;
      for (std::size_t i = 0; i < s.size() && oi < n;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
          out[oi++] = c;
          ++i;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) {
          const unsigned char c2 = static_cast<unsigned char>(s[i + 1]);
          const unsigned cp = ((c & 0x1f) << 6) | (c2 & 0x3f);
          if (cp > 255) {
            throw std::runtime_error(
              "E2E patient text contains characters outside Latin-1"
            );
          }
          out[oi++] = static_cast<std::uint8_t>(cp);
          i += 2;
        } else {
          throw std::runtime_error(
            "E2E patient text contains characters outside Latin-1"
          );
        }
      }
      return out;
    }

    /// Convert an integer Gregorian date to a Julian day number.
    std::int64_t gregorian_to_jdn(int y, int m, int d) {
      const int a = (14 - m) / 12;
      const int yy = y + 4800 - a;
      const int mm = m + 12 * a - 3;
      return d + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    }

    /// Convert a Julian day number to YYYY-MM-DD text.
    std::string jdn_to_iso(std::int64_t J) {
      // Fliegel-Van Flandern integer conversion.
      std::int64_t l = J + 68569;
      const std::int64_t n = 4 * l / 146097;
      l = l - (146097 * n + 3) / 4;
      const std::int64_t i = 4000 * (l + 1) / 1461001;
      l = l - 1461 * i / 4 + 31;
      const std::int64_t j = 80 * l / 2447;
      const int d = static_cast<int>(l - 2447 * j / 80);
      l = j / 11;
      const int m = static_cast<int>(j + 2 - 12 * l);
      const int y = static_cast<int>(100 * (n - 49) + i + l);
      if (y < 1800 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) {
        return {};
      }
      std::ostringstream os;
      os << std::setfill('0') << std::setw(4) << y << '-'
        << std::setw(2) << m << '-' << std::setw(2) << d;
      return os.str();
    }

    /// Convert a Heidelberg birthdate integer using the public reverse-engineered formula.
    std::string e2e_birthdate_iso(std::uint32_t raw) {
      if (raw == 0) {
        return {};
      }
      const std::string digits = std::to_string(raw);
      if (digits.size() == 8 && raw / 10000u >= 1800u && raw / 10000u <= 2200u) {
        const unsigned y = raw / 10000u;
        const unsigned m = (raw / 100u) % 100u;
        const unsigned d = raw % 100u;
        std::ostringstream os;
        os << std::setfill('0') << std::setw(4) << y << '-'
        << std::setw(2) << m << '-' << std::setw(2) << d;
        return os.str();
      }
      const double jd = static_cast<double>(raw) / 64.0 - 14558805.0;
      return jdn_to_iso(static_cast<std::int64_t>(std::floor(jd + 0.5)));
    }

    /// Encode a YYYY-MM-DD date using the same reverse-engineered E2E Julian convention.
    std::uint32_t e2e_birthdate_raw(const std::string& iso) {
      unsigned y = 0, m = 0, d = 0;
      char a = 0, b = 0;
      std::istringstream is(iso);
      is >> y >> a >> m >> b >> d;
      if (
        !is || a != '-' || b != '-' || y < 1800 || y > 2200 ||
        m < 1 || m > 12 || d < 1 || d > 31
      ) {
        throw std::runtime_error("birth_date must be YYYY-MM-DD");
      }
      const auto jd = gregorian_to_jdn(
        static_cast<int>(y),
        static_cast<int>(m),
        static_cast<int>(d)
      );
      const double raw = (static_cast<double>(jd) + 14558805.0) * 64.0;
      if (
        raw < 0 ||
        raw > static_cast<double>(std::numeric_limits<std::uint32_t>::max())
      ) {
        throw std::runtime_error("E2E birth date is out of encodable range");
      }
      return static_cast<std::uint32_t>(std::llround(raw));
    }

    struct E2ERawChunk {
      ChunkInfo info;
      std::uint64_t header_offset {
        0
      };
      std::uint64_t body_offset {
        0
      };
      std::uint32_t body_size {
        0
      };
    };

    class E2EBackend final : public Backend {
      public:
      /// Parse and index one Heidelberg E2E file.
      explicit E2EBackend(const std::string& path) : file_(path), path_(path) {
        parse();
      }
      /// Return E2E format identifier.
      Format format() const noexcept override {
        return Format::E2E;
      }
      /// Return source path.
      const std::string& path() const noexcept override {
        return path_;
      }
      /// Return patient metadata.
      const PatientInfo& patient() const noexcept override {
        return patient_;
      }
      /// Return acquisition metadata.
      const AcquisitionInfo& acquisition() const noexcept override {
        return acquisition_;
      }
      /// Return device metadata.
      const DeviceInfo& device() const noexcept override {
        return device_;
      }
      /// Return scalar metadata.
      const std::unordered_map<std::string, std::string>& metadata() const noexcept override {
        return metadata_;
      }
      /// Return native data elements.
      const std::vector<ChunkInfo>& chunks() const noexcept override {
        return chunks_;
      }
      /// Return OCT volumes.
      const std::vector<VolumeView>& volumes() const noexcept override {
        return volumes_;
      }
      /// Return raw fundus/localizer images.
      const std::vector<FundusView>& fundus_images() const noexcept override {
        return fundus_;
      }
      /// Return layer boundaries.
      const std::vector<SegmentationView>& segmentations() const noexcept override {
        return segmentations_;
      }
      /// Return raw chunk body bytes.
      ArrayView1D chunk_bytes(std::size_t index) const override;
      /// Replace an E2E chunk body with exactly the original byte count.
      void replace_chunk(std::size_t index, const std::vector<std::uint8_t>& bytes) override;
      /// Update fixed-size E2E patient record.
      void set_patient(const PatientInfo& patient) override;
      /// Reject FDA-specific raw-volume replacement.
      void set_fda_raw_volume(
        std::size_t,
        const std::uint16_t*,
        std::size_t,
        std::size_t,
        std::size_t
      ) override {
        throw std::runtime_error("set_fda_raw_volume is FDA-only");
      }
      /// Reject FDA-specific segmentation replacement.
      void set_fda_segmentation(
        const std::string&,
        const std::uint16_t*,
        std::size_t,
        std::size_t
      ) override {
        throw std::runtime_error("set_fda_segmentation is FDA-only");
      }
      /// Reject FDA-specific fundus replacement.
      void set_fda_fundus_jpeg(
        const std::uint8_t*,
        std::size_t,
        std::size_t,
        std::size_t,
        std::size_t
      ) override {
        throw std::runtime_error("set_fda_fundus_jpeg is FDA-only");
      }
      /// Save a lossless E2E copy with same-size body patches.
      void save(const std::string& output_path) const override;
      /// Copy an A-scan to float32.
      void copy_a_scan_f32(
        std::size_t vi,
        std::size_t si,
        std::size_t xi,
        float* out,
        std::size_t n,
        bool decode
      ) const override;
      /// Copy a B-scan to float32.
      void copy_b_scan_f32(
        std::size_t vi,
        std::size_t si,
        float* out,
        std::size_t n,
        bool decode
      ) const override;
      private:
      /// Parse main directories, data elements and supported payloads.
      void parse();
      /// Parse patient body type 9.
      void parse_patient(const std::uint8_t* p, std::size_t n, std::size_t chunk_index);
      /// Return unique volume identifier for a data element.
      static std::string volume_id(const ChunkInfo& c) {
        return std::to_string(c.patient_db_id) + "_" +
              std::to_string(c.study_id) + "_" +
              std::to_string(c.series_id);
      }

      MappedFile file_;
      std::string path_;
      std::uint64_t byte_skip_ {
        0
      };
      std::vector<E2ERawChunk> raw_chunks_;
      std::vector<ChunkInfo> chunks_;
      std::unordered_map<std::size_t, Replacement> replacements_;
      PatientInfo patient_;
      AcquisitionInfo acquisition_;
      DeviceInfo device_;
      std::unordered_map<std::string, std::string> metadata_;
      std::vector<VolumeView> volumes_;
      std::vector<FundusView> fundus_;
      std::vector<SegmentationView> segmentations_;
      std::size_t patient_chunk_index_ {
        std::numeric_limits<std::size_t>::max()
      };
    };

    /// Parse patient body type 9.
    void E2EBackend::parse_patient(const std::uint8_t* p, std::size_t n, std::size_t chunk_index) {
      if (n < 127) {
        return;
      }
      patient_.first_name = latin1_to_utf8(p, 31);
      patient_.last_name = latin1_to_utf8(p + 31, 51);
      const auto birth = read_le<std::uint32_t>(p + 97);
      patient_.birth_date = e2e_birthdate_iso(birth);
      patient_.sex = p[101] ? std::string(1, static_cast<char>(p[101])) : std::string{};
      patient_.patient_id = latin1_to_utf8(p + 102, 25);
      patient_chunk_index_ = chunk_index;
      metadata_["e2e_birthdate_raw"] = std::to_string(birth);
    }

    /// Parse main directories, data elements and supported payloads.
    void E2EBackend::parse() {
      if (file_.size() < 88) {
        throw std::runtime_error("E2E file too small");
      }
      if (file_.size() >= 21 &&
          std::memcmp(file_.data(), "E2EMultipleVolumeFile", 21) == 0) {
        byte_skip_ = 64;
      }
      if (byte_skip_ + 88 > file_.size()) {
        throw std::runtime_error("Invalid E2E header");
      }
      const auto* h = file_.data() + byte_skip_;
      const std::string magic = fixed_string(h, 12);
      metadata_["magic1"] = magic;
      metadata_["version"] = std::to_string(read_le<std::uint32_t>(h + 12));
      const auto* initial = h + 36;
      const std::string magic2 = fixed_string(initial, 12);
      if (magic.empty() || magic2.empty()) {
        throw std::runtime_error("Unrecognized E2E directory header");
      }
      std::vector<std::uint32_t> dirs;
      std::set<std::uint32_t> seen;
      std::uint32_t current = read_le<std::uint32_t>(initial + 40);
      while (current != 0 && seen.insert(current).second) {
        const std::uint64_t off = byte_skip_ + current;
        if (off + 52 > file_.size()) {
          break;
        }
        dirs.push_back(current);
        const auto* d = file_.data() + off;
        current = read_le<std::uint32_t>(d + 44);
      }
      // Some single-volume files keep entries in the immediately following directory.
      if (dirs.empty() && read_le<std::uint32_t>(initial + 36) > 0) {
        dirs.push_back(36);
      }
      std::set<std::uint32_t> starts;
      for (const auto dpos : dirs) {
        const std::uint64_t off = byte_skip_ + dpos;
        if (off + 52 > file_.size()) {
          continue;
        }
        const auto* d = file_.data() + off;
        const auto num = read_le<std::uint32_t>(d + 36);
        if (off + 52ull + 44ull * num > file_.size()) {
          continue;
        }
        for (std::uint32_t i = 0; i < num; ++i) {
          const auto* s = d + 52 + 44ull * i;
          const auto pos = read_le<std::uint32_t>(s);
          const auto start = read_le<std::uint32_t>(s + 4);
          if (start > pos) {
            starts.insert(start);
          }
        }
      }
      std::map<std::string, std::map<std::size_t, BScanView>> volume_slices;
      std::map<std::string, bool> volume_custom;
      std::map<std::string, std::string> laterality;
      for (const auto start : starts) {
        const std::uint64_t hoff = byte_skip_ + start;
        if (hoff + 60 > file_.size()) {
          continue;
        }
        const auto* ch = file_.data() + hoff;
        ChunkInfo ci;
        ci.index = raw_chunks_.size();
        ci.name = "E2E:" + std::to_string(read_le<std::uint32_t>(ch + 52));
        ci.payload_offset = hoff + 60;
        ci.payload_size = read_le<std::uint32_t>(ch + 24);
        ci.patient_db_id = read_le<std::uint32_t>(ch + 32);
        ci.study_id = read_le<std::uint32_t>(ch + 36);
        ci.series_id = read_le<std::uint32_t>(ch + 40);
        ci.slice_id = static_cast<std::int32_t>(read_le<std::uint32_t>(ch + 44));
        ci.indicator = static_cast<std::int16_t>(read_le<std::uint16_t>(ch + 48));
        ci.type = read_le<std::uint32_t>(ch + 52);
        if (ci.payload_offset + ci.payload_size > file_.size()) {
          continue;
        }
        raw_chunks_.push_back( {
          ci, hoff, ci.payload_offset, static_cast<std::uint32_t>(ci.payload_size)
        });
        chunks_.push_back(ci);
        const auto* p = file_.data() + ci.payload_offset;
        const auto n = static_cast<std::size_t>(ci.payload_size);
        const std::string vid = volume_id(ci);
        if (ci.type == 9) {
          parse_patient(p, n, ci.index);
        }
        else if (ci.type == 3 && n >= 5) {
          const char lat = static_cast<char>(p[4]);
          if (lat == 'R' || lat == 'L') {
            laterality[vid] = std::string(1, lat);
          }
        }
        else if (ci.type == 11 && n >= 15) {
          const char lat = static_cast<char>(p[14]);
          if (lat == 'R' || lat == 'L') {
            laterality[vid] = std::string(1, lat);
          }
        }
        else if (ci.type == 10004 && n >= 104) {
          const float scaley = read_f32_le(p + 36);
          metadata_["e2e_scaley"] = std::to_string(scaley);
          const auto ticks = read_le<std::uint64_t>(p + 88);
          if (acquisition_.datetime.empty() && ticks > 116444736000000000ull) {
            const std::uint64_t unixs = ticks / 10000000ull - 11644473600ull;
            acquisition_.datetime = std::to_string(unixs);
            metadata_["acquisition_unix_seconds"] = acquisition_.datetime;
          }
        }
        else if (ci.type == E2E_IMAGE_TYPE && n >= 20) {
          const std::size_t height = read_le<std::uint32_t>(p + 12);
          const std::size_t width = read_le<std::uint32_t>(p + 16);
          const std::size_t count = height * width;
          if (ci.indicator == 1 && 20ull + count * 2ull <= n) {
            BScanView bs;
            bs.slice_index = ci.slice_id >= 0
              ? static_cast<std::size_t>(ci.slice_id / 2)
              : 0;
            bs.pixels.data = p + 20;
            bs.pixels.height = height;
            bs.pixels.width = width;
            bs.pixels.row_stride_bytes = static_cast<std::ptrdiff_t>(width * 2u);
            bs.pixels.dtype = DType::UInt16;
            volume_slices[vid][bs.slice_index] = bs;
            volume_custom[vid] = true;
          } else if (ci.indicator == 0 && 20ull + count <= n) {
            FundusView f;
            f.id = vid;
            f.encoding = ImageEncoding::Raw;
            f.data = p + 20;
            f.byte_size = count;
            f.height = height;
            f.width = width;
            f.channels = 1;
            f.dtype = DType::UInt8;
            fundus_.push_back(f);
          }
        }
        else if (ci.type == E2E_CONTOUR_V2 && n >= 36) {
          const int id = static_cast<int>(read_le<std::uint32_t>(p + 4));
          const std::size_t width = read_le<std::uint32_t>(p + 12);
          if (36ull + width * 4ull <= n) {
            SegmentationView s;
            s.layer = "contour" + std::to_string(id);
            s.layer_id = id;
            s.slice_index = ci.slice_id >= 0
              ? static_cast<std::size_t>(ci.slice_id / 2)
              : 0;
            s.values.data = p + 36;
            s.values.height = 1;
            s.values.width = width;
            s.values.row_stride_bytes = static_cast<std::ptrdiff_t>(width * 4u);
            s.values.dtype = DType::Float32;
            segmentations_.push_back(s);
          }
        }
        else if (ci.type == E2E_CONTOUR_V1 && n >= 16) {
          const int id = static_cast<int>(read_le<std::uint32_t>(p + 4));
          const std::size_t width = read_le<std::uint32_t>(p + 12);
          if (16ull + width * 4ull <= n) {
            SegmentationView s;
            s.layer = "contour" + std::to_string(id);
            s.layer_id = id;
            s.slice_index = ci.slice_id >= 0
              ? static_cast<std::size_t>(ci.slice_id / 2)
              : 0;
            s.values.data = p + 16;
            s.values.height = 1;
            s.values.width = width;
            s.values.row_stride_bytes = static_cast<std::ptrdiff_t>(width * 4u);
            s.values.dtype = DType::Float32;
            segmentations_.push_back(s);
          }
        }
        else if (ci.type == 9001 && n >= 8) {
          device_.model = "Heidelberg";
        }
      }
      for (auto& kv : volume_slices) {
        VolumeView v;
        v.id = kv.first;
        v.e2e_custom_float = volume_custom[kv.first];
        for (auto& sv : kv.second) {
          v.slices.push_back(sv.second);
        }
        auto it = laterality.find(kv.first);
        if (acquisition_.laterality.empty() && it != laterality.end()) {
          acquisition_.laterality = it->second;
        }
        volumes_.push_back(std::move(v));
      }
      metadata_["directory_count"] = std::to_string(dirs.size());
      metadata_["chunk_count"] = std::to_string(chunks_.size());
      metadata_["byte_skip"] = std::to_string(byte_skip_);
    }

    /// Return raw chunk body bytes.
    ArrayView1D E2EBackend::chunk_bytes(std::size_t index) const {
      if (index >= raw_chunks_.size()) {
        throw std::out_of_range("E2E chunk index");
      }
      auto it = replacements_.find(index);
      if (it != replacements_.end()) {
        return {
          it->second.bytes.data(), it->second.bytes.size(), 1, DType::UInt8
        };
      }
      const auto& c = raw_chunks_[index];
      return {
        checked_span(file_, c.body_offset, c.body_size), c.body_size, 1, DType::UInt8
      };
    }

    /// Replace an E2E chunk body with exactly the original byte count.
    void E2EBackend::replace_chunk(std::size_t index, const std::vector<std::uint8_t>& bytes) {
      if (index >= raw_chunks_.size()) {
        throw std::out_of_range("E2E chunk index");
      }
      if (bytes.size() != raw_chunks_[index].body_size) {
        throw std::runtime_error(
          "E2E safe writer only accepts same-size chunk replacements because "
          "directory offsets are absolute"
        );
      }
      replacements_[index].bytes = bytes;
    }

    /// Update fixed-size E2E patient record.
    void E2EBackend::set_patient(const PatientInfo& patient) {
      if (patient_chunk_index_ == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("E2E patient chunk not found");
      }
      auto v = chunk_bytes(patient_chunk_index_);
      if (v.length < 127) {
        throw std::runtime_error("E2E patient chunk is truncated");
      }
      const auto* begin = static_cast<const std::uint8_t*>(v.data);
      std::vector<std::uint8_t> b(begin, begin + v.length);
      auto f = utf8_to_latin1_fixed(patient.first_name, 31);
      std::copy(f.begin(), f.end(), b.begin());
      auto l = utf8_to_latin1_fixed(patient.last_name, 51);
      std::copy(l.begin(), l.end(), b.begin() + 31);
      if (!patient.birth_date.empty()) {
        write_le<std::uint32_t>(
          b.data() + 97,
          e2e_birthdate_raw(patient.birth_date)
        );
      }
      b[101] = patient.sex.empty()
        ? 0
        : static_cast<std::uint8_t>(patient.sex[0]);
      auto id = utf8_to_latin1_fixed(patient.patient_id, 25);
      std::copy(id.begin(), id.end(), b.begin() + 102);
      replacements_[patient_chunk_index_].bytes = std::move(b);
      patient_ = patient;
    }

    /// Save a lossless E2E copy with same-size body patches.
    void E2EBackend::save(const std::string& output_path) const {
      std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
      if (!out) {
        throw std::runtime_error("Cannot create output: " + output_path);
      }
      out.write(
        reinterpret_cast<const char*>(file_.data()),
        static_cast<std::streamsize>(file_.size())
      );
      if (!out) {
        throw std::runtime_error("Failed copying E2E source");
      }
      for (const auto& kv : replacements_) {
        const auto& c = raw_chunks_.at(kv.first);
        out.seekp(static_cast<std::streamoff>(c.body_offset));
        out.write(
          reinterpret_cast<const char*>(kv.second.bytes.data()),
          static_cast<std::streamsize>(kv.second.bytes.size())
        );
      }
      if (!out) {
        throw std::runtime_error("Failed patching E2E output");
      }
    }

    /// Copy an A-scan to float32.
    void E2EBackend::copy_a_scan_f32(
      std::size_t vi,
      std::size_t si,
      std::size_t xi,
      float* out,
      std::size_t n,
      bool decode
    ) const {
      if (vi >= volumes_.size() || si >= volumes_[vi].slices.size()) {
        throw std::out_of_range("Volume/slice index");
      }
      const auto& a = volumes_[vi].slices[si].pixels;
      if (xi >= a.width || n < a.height) {
        throw std::runtime_error(
          "A-scan output buffer too small or x out of range"
        );
      }
      const auto* base = static_cast<const std::uint8_t*>(a.data);
      for (std::size_t y = 0; y < a.height; ++y) {
        const auto raw = read_le<std::uint16_t>(
          base + y * a.row_stride_bytes + xi * 2u
        );
        float v = decode
          ? e2e_ufloat16_to_float(raw)
          : static_cast<float>(raw);
        out[y] = decode ? e2e_display_transform(v) : v;
      }
    }

    /// Copy a B-scan to float32.
    void E2EBackend::copy_b_scan_f32(
      std::size_t vi,
      std::size_t si,
      float* out,
      std::size_t n,
      bool decode
    ) const {
      if (vi >= volumes_.size() || si >= volumes_[vi].slices.size()) {
        throw std::out_of_range("Volume/slice index");
      }
      const auto& a = volumes_[vi].slices[si].pixels;
      if (n < a.height * a.width) {
        throw std::runtime_error("B-scan output buffer too small");
      }
      const auto* base = static_cast<const std::uint8_t*>(a.data);
      for (std::size_t y = 0; y < a.height; ++y) {
        for (std::size_t x = 0; x < a.width; ++x) {
          const auto raw = read_le<std::uint16_t>(
            base + y * a.row_stride_bytes + x * 2u
          );
        float v = decode
          ? e2e_ufloat16_to_float(raw)
          : static_cast<float>(raw);
          out[y * a.width + x] = decode ? e2e_display_transform(v) : v;
        }
      }
    }

  }
  // namespace

  /// Construct a Heidelberg E2E backend.
  std::unique_ptr<Backend> make_e2e_backend(const std::string& path) {
    return std::make_unique<E2EBackend>(path);
  }

}
// namespace octio
