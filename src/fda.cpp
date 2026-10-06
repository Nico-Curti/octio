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
#include <sstream>
#include <stdexcept>

namespace octio {
  namespace {

    constexpr std::size_t FDA_HEADER_SIZE = 15;

    // Full 615-byte modal keystream inferred from the supplied Triton corpus.
    static const char* kPatientModeHex =
    "8243037f654007670b8e3f5390fee641ccab389c0c8a0233ae11d019c10e4cdc"
    "9018e43cc46951b1794316677ae4eeb8a01641f967c4ef819261639d9f50b361"
    "7c0c2e1cfd2e6f5e2c629181bf1939798fbc7a54403102f9c3033cd0811f83d9"
    "2d3cbb160e22a38d3d6ba33dd10f7eea5911c20a8a5da5a0744376d1cbd8bdc4"
    "948ef9779a6fcd73301795384eb74bd1ab4866c4d2fa952893603405c7a3c42ea"
    "6986b671af6efcd52f2398830017ea9f62bcebb6014cf047d1091281eb6ed19f1"
    "3e6cbc8051dabe5e54cdde837d1ecb46988d67230785b47514f841073eca407a0"
    "c52f7df1941a637a1401a4334d78bacb5b2d7b027d34ff0792c97c15d2009583"
    "c15374b5ec3f6f05acd5c60abf2a2a88ecbb39768b4c1334326f10a5dd288565"
    "707092878e9d2d4187d6fbc0892926bec217d91e204e232a33432144c4d6e42d3"
    "f52a756bcbab75753ee48da04c9f0a10250c9d232d82f117f42f49a9f11b98cc"
    "9ef8f8f35b48f331ebf1891a1d1f5bfd79ce72e03edf1f70d58612d13dd27465"
    "70853c755e3f63168926333afa621141684043516c439794649c197d4de453934e"
    "721a2cec6466a695b36d06f6767b3a5361dc0e2d6fad9de7c3f661878104d1de"
    "46b71f313451bc5284299af90bfec48755e403433321031236bb96793913f9522"
    "ec49a72d5ffa22ad5a5329f5cb59fc46cb9e0763a5d09436b5264b6e7c5170cc"
    "e948c22caa6fe9b3b117092b348c090af2f74db17a6a54b34c25318cedfc72eb4"
    "41cb3456abed2dc53abc154e88fd5f9bb059af32f9cbb230e798b4f9ffd39d0dc"
    "e249cfd1faf77c4acf56c07fc95666ffc0094bf8c1b0b7391f1d19e05ab745a445eda";

    static const char* kPatientIdHex =
      "e1227112042e36670b8e3f5390fee641ccab389c0c8a0233ae11d019c10e4cdc";
    static const char* kFirstPrefixHex = "dd798a55a56951b1794316677ae4eeb8ae";
    static const char* kLastPrefixHex = "316d5c7d942e6f5e2c629181bf19391580a01330";
    static const char* kBirthHex = "a2a43ad11c7e";
    constexpr std::uint8_t kSexKey = 0x3c;

    /// Decode a hexadecimal constant into bytes.
    std::vector<std::uint8_t> hex_bytes(const char* s) {
      std::vector<std::uint8_t> out;
      const std::string str(s);
      if (str.size() % 2) {
        throw std::runtime_error("Invalid internal hex constant");
      }
      out.reserve(str.size() / 2);
      auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') {
          return static_cast<unsigned>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
          return 10u + static_cast<unsigned>(c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
          return 10u + static_cast<unsigned>(c - 'A');
        }
        throw std::runtime_error("Invalid internal hex constant");
      };
      for (std::size_t i = 0; i < str.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(str[i]) << 4u) | nibble(str[i + 1])));
      }
      return out;
    }

    /// Return the globally inferred PATIENT_INFO_03 zero/plaintext keystream.
    const std::vector<std::uint8_t>& patient_mode_key() {
      static const auto key = hex_bytes(kPatientModeHex);
      return key;
    }

    /// Return the patient ID keystream.
    const std::vector<std::uint8_t>& patient_id_key() {
      static const auto key = hex_bytes(kPatientIdHex);
      return key;
    }

    /// Return the first-name keystream including modal padding tail.
    std::vector<std::uint8_t> first_name_key() {
      auto key = std::vector<std::uint8_t>(
        patient_mode_key().begin() + 32,
        patient_mode_key().begin() + 64
      );
      auto prefix = hex_bytes(kFirstPrefixHex);
      std::copy(prefix.begin(), prefix.end(), key.begin());
      return key;
    }

    /// Return the last-name keystream including modal padding tail.
    std::vector<std::uint8_t> last_name_key() {
      auto key = std::vector<std::uint8_t>(
        patient_mode_key().begin() + 64,
        patient_mode_key().begin() + 96
      );
      auto prefix = hex_bytes(kLastPrefixHex);
      std::copy(prefix.begin(), prefix.end(), key.begin());
      return key;
    }

    /// Convert a Y/M/D tuple to ISO text when valid.
    std::string date_iso(std::uint16_t y, std::uint16_t m, std::uint16_t d) {
      if (y == 0 && m == 0 && d == 0) {
        return {};
      }
      if (y < 1800 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) {
        return {};
      }
      static const int mdays[] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
      };
      int maxd = mdays[m];
      const bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
      if (m == 2 && leap) {
        maxd = 29;
      }
      if (d > maxd) {
        return {};
      }
      std::ostringstream os;
      os << std::setfill('0') << std::setw(4) << y << '-'
        << std::setw(2) << m << '-' << std::setw(2) << d;
      return os.str();
    }

    /// Convert six uint16 date-time fields to ISO text when valid.
    std::string datetime_iso(const std::uint8_t* p) {
      const auto y = read_le<std::uint16_t>(p + 0);
      const auto m = read_le<std::uint16_t>(p + 2);
      const auto d = read_le<std::uint16_t>(p + 4);
      const auto hh = read_le<std::uint16_t>(p + 6);
      const auto mm = read_le<std::uint16_t>(p + 8);
      const auto ss = read_le<std::uint16_t>(p + 10);
      if (date_iso(y, m, d).empty() || hh > 23 || mm > 59 || ss > 59) {
        return {};
      }
      std::ostringstream os;
      os << date_iso(y, m, d) << 'T'
        << std::setfill('0') << std::setw(2) << hh << ':'
        << std::setw(2) << mm << ':'
        << std::setw(2) << ss;
      return os.str();
    }

    /// Return a canonical Topcon layer name for a MULTILAYERS identifier.
    std::string canonical_layer(const std::string& id) {
      static const std::map<std::string, std::string> map = {
        {
          "MULTILAYERS_1", "ILM"
        }, {
          "MULTILAYERS_2", "RNFL_GCL"
        }, {
          "MULTILAYERS_3", "GCL_IPL"
        },
        {
          "MULTILAYERS_4", "IPL_INL"
        }, {
          "MULTILAYERS_5", "MZ_EZ"
        }, {
          "MULTILAYERS_6", "IZ_RPE"
        },
        {
          "MULTILAYERS_7", "BM"
        }, {
          "MULTILAYERS_8", "INL_OPL"
        }, {
          "MULTILAYERS_9", "ELM"
        },
        {
          "MULTILAYERS_10", "CSI"
        }
      };
      auto it = map.find(id);
      return it == map.end() ? id : it->second;
    }

    /// Return the native MULTILAYERS identifier for a canonical layer name.
    std::string native_layer(const std::string& name) {
      static const std::map<std::string, std::string> map = {
        {
          "ILM", "MULTILAYERS_1"
        }, {
          "RNFL_GCL", "MULTILAYERS_2"
        }, {
          "GCL_IPL", "MULTILAYERS_3"
        },
        {
          "IPL_INL", "MULTILAYERS_4"
        }, {
          "MZ_EZ", "MULTILAYERS_5"
        }, {
          "IZ_RPE", "MULTILAYERS_6"
        },
        {
          "BM", "MULTILAYERS_7"
        }, {
          "INL_OPL", "MULTILAYERS_8"
        }, {
          "ELM", "MULTILAYERS_9"
        },
        {
          "CSI", "MULTILAYERS_10"
        }
      };
      auto it = map.find(name);
      return it == map.end() ? name : it->second;
    }

    struct FdaRawChunk {
      ChunkInfo info;
      std::string native_name;
    };

    class FdaBackend final : public Backend {
      public:
      /// Parse and index one FDA file using a memory mapping.
      explicit FdaBackend(const std::string& path) : file_(path), path_(path) {
        parse();
      }
      /// Return FDA format identifier.
      Format format() const noexcept override {
        return Format::FDA;
      }
      /// Return source path.
      const std::string& path() const noexcept override {
        return path_;
      }
      /// Return decoded patient metadata.
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
      /// Return indexed native chunks.
      const std::vector<ChunkInfo>& chunks() const noexcept override {
        return chunks_;
      }
      /// Return OCT volumes.
      const std::vector<VolumeView>& volumes() const noexcept override {
        return volumes_;
      }
      /// Return fundus/localizer images.
      const std::vector<FundusView>& fundus_images() const noexcept override {
        return fundus_;
      }
      /// Return segmentation arrays.
      const std::vector<SegmentationView>& segmentations() const noexcept override {
        return segmentations_;
      }
      /// Return a zero-copy chunk byte view unless it has been replaced.
      ArrayView1D chunk_bytes(std::size_t index) const override;
      /// Replace one length-prefixed FDA payload.
      void replace_chunk(std::size_t index, const std::vector<std::uint8_t>& bytes) override;
      /// Re-encode PATIENT_INFO_03 fields.
      void set_patient(const PatientInfo& patient) override;
      /// Replace raw OCT volume pixels and header dimensions.
      void set_fda_raw_volume(
        std::size_t volume_index,
        const std::uint16_t* data,
        std::size_t slices,
        std::size_t height,
        std::size_t width
      ) override;
      /// Replace or append a layer segmentation.
      void set_fda_segmentation(
        const std::string& layer,
        const std::uint16_t* data,
        std::size_t height,
        std::size_t width
      ) override;
      /// Replace or append a JPEG fundus payload.
      void set_fda_fundus_jpeg(
        const std::uint8_t* jpeg,
        std::size_t size,
        std::size_t height,
        std::size_t width,
        std::size_t channels
      ) override;
      /// Rebuild FDA chunk stream preserving unknown payloads and chunk order.
      void save(const std::string& output_path) const override;
      /// Copy one A-scan to float32.
      void copy_a_scan_f32(
        std::size_t vi,
        std::size_t si,
        std::size_t xi,
        float* out,
        std::size_t n,
        bool
      ) const override;
      /// Copy one B-scan to float32.
      void copy_b_scan_f32(
        std::size_t vi,
        std::size_t si,
        float* out,
        std::size_t n,
        bool
      ) const override;
      private:
      /// Parse header, chunks and supported structures.
      void parse();
      /// Parse PATIENT_INFO_02/03.
      void parse_patient(const std::uint8_t* p, std::size_t n, const std::string& chunk_name);
      /// Parse CAPTURE_INFO_02.
      void parse_capture(const std::uint8_t* p, std::size_t n);
      /// Parse hardware information.
      void parse_hw(const std::uint8_t* p, std::size_t n);
      /// Parse scan dimensions and spacing.
      void parse_scan_params(const std::uint8_t* p, std::size_t n, const std::string& name);
      /// Parse raw OCT volume if present.
      void parse_raw_volume(const FdaRawChunk& c);
      /// Parse JPEG-compressed OCT volume when no raw volume is available.
      void parse_jpeg_volume(const FdaRawChunk& c);
      /// Parse fundus/localizer image descriptors.
      void parse_fundus(const FdaRawChunk& c);
      /// Parse a CONTOUR_INFO layer.
      void parse_contour(const FdaRawChunk& c);
      /// Find first chunk with a given native name.
      std::size_t find_chunk(const std::string& name) const;

      MappedFile file_;
      std::string path_;
      std::array<std::uint8_t, FDA_HEADER_SIZE> header_{};
      std::vector<FdaRawChunk> raw_chunks_;
      std::vector<ChunkInfo> chunks_;
      std::unordered_map<std::size_t, Replacement> replacements_;
      PatientInfo patient_;
      AcquisitionInfo acquisition_;
      DeviceInfo device_;
      std::unordered_map<std::string, std::string> metadata_;
      std::vector<VolumeView> volumes_;
      std::vector<FundusView> fundus_;
      std::vector<SegmentationView> segmentations_;
      double spacing_x_ {
        0.0
      }, spacing_y_ {
        0.0
      }, spacing_z_ {
        0.0
      };
    };

    /// Find first chunk with a given native name.
    std::size_t FdaBackend::find_chunk(const std::string& name) const {
      for (std::size_t i = 0; i < raw_chunks_.size(); ++i) {
        if (raw_chunks_[i].native_name == name) {
          return i;
        }
      }
      return std::numeric_limits<std::size_t>::max();
    }

    /// Parse PATIENT_INFO_02/03.
    void FdaBackend::parse_patient(
      const std::uint8_t* p,
      std::size_t n,
      const std::string& chunk_name
    ) {
      if (n < 181) return;
      std::vector<std::uint8_t> plain;
      if (chunk_name == "@PATIENT_INFO_03") {
        if (n < 615) return;
        plain.resize(615);
        const auto& mode = patient_mode_key();
        for (std::size_t i = 0; i < 615; ++i) plain[i] = p[i] ^ mode[i];
        const auto pid_key = patient_id_key();
        for (std::size_t i = 0; i < 32; ++i) plain[i] = p[i] ^ pid_key[i];
        const auto fk = first_name_key();
        const auto lk = last_name_key();
        for (std::size_t i = 0; i < 32; ++i) plain[32+i] = p[32+i] ^ fk[i];
        for (std::size_t i = 0; i < 32; ++i) plain[64+i] = p[64+i] ^ lk[i];
        plain[104] = p[104] ^ kSexKey;
        const auto bk = hex_bytes(kBirthHex);
        for (std::size_t i = 0; i < 6; ++i) plain[105+i] = p[105+i] ^ bk[i];
        p = plain.data();
        n = plain.size();
        metadata_["patient_info_obfuscation"] = "xor03";
      } else {
        metadata_["patient_info_obfuscation"] = "none";
      }
      patient_.patient_id = fixed_string(p + 0, 32);
      patient_.first_name = fixed_string(p + 32, 32);
      patient_.last_name = fixed_string(p + 64, 32);
      patient_.middle_name = fixed_string(p + 96, 8);
      const auto sex = p[104];
      patient_.sex = sex == 1 ? "M" : sex == 2 ? "F" : sex == 3 ? "O" : "";
      patient_.birth_date = date_iso(
        read_le<std::uint16_t>(p + 105),
        read_le<std::uint16_t>(p + 107),
        read_le<std::uint16_t>(p + 109)
      );
      if (n >= 307) patient_.physician = fixed_string(p + 179, 128);
      if (n >= 319) patient_.zip_code = fixed_string(p + 307, 12);
      if (n >= 415) patient_.address = fixed_string(p + 319, 96);
      if (n >= 447) patient_.phones = fixed_string(p + 415, 32);
      if (n >= 583) patient_.description = fixed_string(p + 519, 64);
      if (n >= 179) {
        const auto lv = date_iso(
          read_le<std::uint16_t>(p + 173),
          read_le<std::uint16_t>(p + 175),
          read_le<std::uint16_t>(p + 177)
        );
        if (!lv.empty()) metadata_["last_visit_date"] = lv;
      }
    }

    /// Parse CAPTURE_INFO_02.
    void FdaBackend::parse_capture(const std::uint8_t* p, std::size_t n) {
      if (n < 13) return;
      acquisition_.laterality = p[0] == 0 ? "R" : p[0] == 1 ? "L" : "";
      if (n >= 118) {
        acquisition_.scan_mode = p[1];
        acquisition_.session_id = read_le<std::uint32_t>(p+2);
        acquisition_.label = fixed_string(p+6, 100);
        acquisition_.datetime = datetime_iso(p+106);
      } else {
        acquisition_.datetime = datetime_iso(p+1);
      }
    }

    /// Parse hardware information.
    void FdaBackend::parse_hw(const std::uint8_t* p, std::size_t n) {
      if (n < 64) return;
      device_.model = fixed_string(p+0, 16);
      device_.serial = fixed_string(p+16, 16);
      device_.spectrometer_serial = fixed_string(p+32, 16);
      device_.rom_version = fixed_string(p+48, 16);
    }

    /// Parse scan dimensions and spacing.
    void FdaBackend::parse_scan_params(
      const std::uint8_t* p,
      std::size_t n,
      const std::string& name
    ) {
      if (name == "@PARAM_SCAN_04" && n >= 55) {
        metadata_["fixation"] = std::to_string(read_le<std::uint32_t>(p));
        metadata_["mirror_pos"] = std::to_string(read_le<std::uint32_t>(p+4));
        metadata_["polar"] = std::to_string(read_le<std::uint32_t>(p+8));
        const double x = read_f64_le(p+12), y = read_f64_le(p+20), z = read_f64_le(p+28);
        metadata_["x_dimension_mm"] = std::to_string(x);
        metadata_["y_dimension_mm"] = std::to_string(y);
        metadata_["z_resolution_um"] = std::to_string(z);
        spacing_z_ = z / 1000.0;
        metadata_["scan_param_version"] = "04";
        // x/y spacing are finalized once volume shape is known.
        metadata_["_x_mm"] = std::to_string(x);
        metadata_["_y_mm"] = std::to_string(y);
      } else if (name == "@PARAM_SCAN_02" && n >= 82) {
        const double x = read_f64_le(p+21), y = read_f64_le(p+29), z = read_f64_le(p+37);
        metadata_["x_dimension_mm"] = std::to_string(x);
        metadata_["y_dimension_mm"] = std::to_string(y);
        metadata_["z_resolution_um"] = std::to_string(z);
        spacing_z_ = z / 1000.0;
        metadata_["scan_param_version"] = "02";
        metadata_["_x_mm"] = std::to_string(x);
        metadata_["_y_mm"] = std::to_string(y);
      }
    }

    /// Parse raw OCT volume if present.
    void FdaBackend::parse_raw_volume(const FdaRawChunk& c) {
      const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
      if (c.info.payload_size < 22) return;
      const std::size_t w = read_le<std::uint32_t>(p+1);
      const std::size_t h = read_le<std::uint32_t>(p+5);
      const std::size_t bpp = read_le<std::uint32_t>(p+9);
      const std::size_t s = read_le<std::uint32_t>(p+13);
      if (bpp != 16 || w == 0 || h == 0 || s == 0) {
        return;
      }
      const std::uint64_t bytes = static_cast<std::uint64_t>(w)*h*s*2u;
      if (22u + bytes > c.info.payload_size) return;
      VolumeView vol;
      vol.id = "topcon_primary";
      vol.slices.reserve(s);
      const auto* base = p + 22;
      for (std::size_t z = 0; z < s; ++z) {
        BScanView bs;
        bs.slice_index = z;
        bs.pixels.data = base + z*w*h*2u;
        bs.pixels.height = h;
        bs.pixels.width = w;
        bs.pixels.row_stride_bytes = static_cast<std::ptrdiff_t>(w*2u);
        bs.pixels.dtype = DType::UInt16;
        vol.slices.push_back(bs);
      }
      double x = 0, y = 0;
      auto itx = metadata_.find("_x_mm"), ity = metadata_.find("_y_mm");
      if (itx != metadata_.end()) x = std::stod(itx->second);
      if (ity != metadata_.end()) y = std::stod(ity->second);
      vol.spacing_x_mm = w ? x/static_cast<double>(w) : 0.0;
      vol.spacing_y_mm = s ? y/static_cast<double>(s) : 0.0;
      vol.spacing_z_mm = spacing_z_;
      volumes_.push_back(std::move(vol));
      metadata_["oct_width"] = std::to_string(w);
      metadata_["oct_height"] = std::to_string(h);
      metadata_["oct_slices"] = std::to_string(s);
      metadata_["oct_bits_per_pixel"] = std::to_string(bpp);
      metadata_["oct_raw_trailer_bytes"] = std::to_string(c.info.payload_size - 22u - bytes);
      metadata_["oct_storage"] = "IMG_MOT_COMP_03";
    }

    /// Parse JPEG-compressed OCT volume when no raw volume is available.
    void FdaBackend::parse_jpeg_volume(const FdaRawChunk& c) {
      // Prefer IMG_MOT_COMP_03 when both representations are present because it
      // enables true zero-copy pixel access and avoids a JPEG dependency.
      if (!volumes_.empty()) {
        return;
      }
      const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
      if (c.info.payload_size < 25) return;
      const std::size_t w = read_le<std::uint32_t>(p + 9);
      const std::size_t h = read_le<std::uint32_t>(p + 13);
      const std::size_t slices = read_le<std::uint32_t>(p + 17);
      if (w == 0 || h == 0 || slices == 0) {
        return;
      }
      std::size_t pos = 25;
      VolumeView vol;
      vol.id = "topcon_jpeg";
      vol.slices.reserve(slices);
      for (std::size_t z = 0; z < slices; ++z) {
        if (pos + 4 > c.info.payload_size) return;
        const std::size_t size = read_le<std::uint32_t>(p + pos);
        pos += 4;
        if (size == 0 || pos + size > c.info.payload_size) return;
        BScanView bs;
        bs.slice_index = z;
        bs.encoding = ImageEncoding::JPEG;
        bs.encoded_data = p + pos;
        bs.encoded_size = size;
        bs.encoded_height = h;
        bs.encoded_width = w;
        vol.slices.push_back(bs);
        pos += size;
      }
      vol.spacing_x_mm = spacing_x_;
      vol.spacing_y_mm = spacing_y_;
      vol.spacing_z_mm = spacing_z_;
      volumes_.push_back(std::move(vol));
      metadata_["oct_storage"] = "IMG_JPEG";
    }

    /// Parse fundus/localizer image descriptors.
    void FdaBackend::parse_fundus(const FdaRawChunk& c) {
      const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
      FundusView v;
      v.id = c.native_name;
      v.encoding = ImageEncoding::JPEG;
      v.dtype = DType::UInt8;
      if (c.native_name == "@IMG_FUNDUS" && c.info.payload_size >= 24) {
        v.width = read_le<std::uint32_t>(p);
        v.height = read_le<std::uint32_t>(p+4);
        const auto bpp = read_le<std::uint32_t>(p+8);
        v.channels = std::max<std::size_t>(1, bpp/8u);
        const std::size_t size = read_le<std::uint32_t>(p+20);
        if (24u + size <= c.info.payload_size) {
          v.data = p+24;
          v.byte_size = size;
          fundus_.push_back(v);
        }
      } else if (c.native_name == "@IMG_TRC_02" && c.info.payload_size >= 21) {
        v.width = read_le<std::uint32_t>(p);
        v.height = read_le<std::uint32_t>(p+4);
        const auto bpp = read_le<std::uint32_t>(p+8);
        v.channels = std::max<std::size_t>(1, bpp/8u);
        const std::size_t size = read_le<std::uint32_t>(p+17);
        if (21u + size <= c.info.payload_size) {
          v.data = p+21;
          v.byte_size = size;
          fundus_.push_back(v);
        }
      }
    }

    /// Parse a CONTOUR_INFO layer.
    void FdaBackend::parse_contour(const FdaRawChunk& c) {
      const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
      if (c.info.payload_size < 34) return;
      const std::string id = fixed_string(p, 20);
      const std::size_t w = read_le<std::uint32_t>(p+22);
      const std::size_t h = read_le<std::uint32_t>(p+26);
      const std::uint64_t bytes = static_cast<std::uint64_t>(w)*h*2u;
      if (w == 0 || h == 0 || 34u+bytes > c.info.payload_size) return;
      SegmentationView s;
      s.layer = canonical_layer(id);
      if (id.rfind("MULTILAYERS_", 0) == 0) {
        try {
          s.layer_id = std::stoi(id.substr(12));
        } catch (...) {}
      }
      s.values.data = p+34;
      s.values.height = h;
      s.values.width = w;
      s.values.row_stride_bytes = static_cast<std::ptrdiff_t>(w*2u);
      s.values.dtype = DType::UInt16;
      s.measured_from_bottom = true;
      segmentations_.push_back(s);
    }

    /// Parse header, chunks and supported structures.
    void FdaBackend::parse() {
      if (
        file_.size() < FDA_HEADER_SIZE ||
        std::memcmp(file_.data(), "FOCTFDA", 7) != 0
      ) {
        throw std::runtime_error("Invalid FDA header");
      }
      std::memcpy(header_.data(), file_.data(), FDA_HEADER_SIZE);
      metadata_["file_code"] = fixed_string(file_.data(), 4);
      metadata_["file_type"] = fixed_string(file_.data()+4, 3);
      metadata_["major_version"] = std::to_string(read_le<std::uint32_t>(file_.data()+7));
      metadata_["minor_version"] = std::to_string(read_le<std::uint32_t>(file_.data()+11));
      std::size_t pos = FDA_HEADER_SIZE, idx = 0;
      while (pos < file_.size()) {
        const std::uint8_t n = file_.data()[pos++];
        if (n == 0) break;
        if (pos+n+4 > file_.size()) throw std::runtime_error("Truncated FDA chunk header");
        std::string name(reinterpret_cast<const char*>(file_.data()+pos), n);
        pos += n;
        const auto size = read_le<std::uint32_t>(file_.data()+pos);
        pos += 4;
        if (size > file_.size() - pos) {
          throw std::runtime_error("FDA chunk exceeds file size: " + name);
        }
        ChunkInfo ci;
        ci.index = idx;
        ci.name = name;
        ci.payload_offset = pos;
        ci.payload_size = size;
        raw_chunks_.push_back( {
          ci, name
        });
        chunks_.push_back(ci);
        pos += size;
        ++idx;
      }
      // Parse metadata before volume so pixel spacing is already known.
      for (const auto& c: raw_chunks_) {
        const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
        if (
          c.native_name == "@PATIENT_INFO_03" ||
          c.native_name == "@PATIENT_INFO_02" ||
          c.native_name == "@PATIENT_INFO"
        ) {
          parse_patient(p, c.info.payload_size, c.native_name);
        }
        else if (
          c.native_name == "@CAPTURE_INFO_02" ||
          c.native_name == "@CAPTURE_INFO"
        ) {
          parse_capture(p, c.info.payload_size);
        }
        else if (c.native_name.rfind("@HW_INFO_", 0) == 0) parse_hw(p, c.info.payload_size);
        else if (
          c.native_name == "@PARAM_SCAN_04" ||
          c.native_name == "@PARAM_SCAN_02"
        ) {
          parse_scan_params(p, c.info.payload_size, c.native_name);
        }
        else if (c.native_name == "@MAIN_MODULE_INFO" && c.info.payload_size >= 264) {
          device_.software_name = fixed_string(p, 128);
          std::ostringstream v;
          v << read_le<std::uint16_t>(p + 128) << '.'
            << read_le<std::uint16_t>(p + 130) << '.'
            << read_le<std::uint16_t>(p + 132) << '.'
            << read_le<std::uint16_t>(p + 134);
          device_.software_version = v.str();
        }
      }
      for (const auto& c: raw_chunks_) {
        if (c.native_name == "@IMG_MOT_COMP_03") parse_raw_volume(c);
      }
      for (const auto& c: raw_chunks_) {
        if (c.native_name == "@IMG_JPEG") parse_jpeg_volume(c);
        else if (c.native_name == "@IMG_FUNDUS" || c.native_name == "@IMG_TRC_02") parse_fundus(c);
        else if (c.native_name == "@CONTOUR_INFO") parse_contour(c);
      }
      metadata_.erase("_x_mm");
      metadata_.erase("_y_mm");
    }

    /// Return a zero-copy chunk byte view unless it has been replaced.
    ArrayView1D FdaBackend::chunk_bytes(std::size_t index) const {
      if (index >= raw_chunks_.size()) throw std::out_of_range("FDA chunk index");
      auto it = replacements_.find(index);
      if (it != replacements_.end()) return {
        it->second.bytes.data(), it->second.bytes.size(), 1, DType::UInt8
      };
      const auto& c = raw_chunks_[index].info;
      return {
        checked_span(file_, c.payload_offset, c.payload_size),
        static_cast<std::size_t>(c.payload_size),
        1,
        DType::UInt8
      };
    }

    /// Replace one length-prefixed FDA payload.
    void FdaBackend::replace_chunk(std::size_t index, const std::vector<std::uint8_t>& bytes) {
      if (index >= raw_chunks_.size()) throw std::out_of_range("FDA chunk index");
      replacements_[index].bytes = bytes;
    }

    /// Re-encode PATIENT_INFO_03 fields.
    void FdaBackend::set_patient(const PatientInfo& patient) {
      const auto idx = find_chunk("@PATIENT_INFO_03");
      if (idx == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(
          "PATIENT_INFO_03 not present; safe obfuscated writing is unavailable"
        );
      }
      auto view = chunk_bytes(idx);
      if (view.length<615) throw std::runtime_error("PATIENT_INFO_03 is shorter than 615 bytes");
      const auto* begin = static_cast<const std::uint8_t*>(view.data);
      std::vector<std::uint8_t> raw(begin, begin + view.length);
      auto write_text = [&raw](
        std::size_t off,
        std::size_t n,
        const std::string& s,
        const std::vector<std::uint8_t>& key
      ) {
        if (s.size() > n) {
          throw std::runtime_error("Patient field exceeds native FDA width");
        }
        std::vector<std::uint8_t> plain(n, 0);
        std::memcpy(plain.data(), s.data(), s.size());
        for (std::size_t i = 0;i<n;++i) raw[off+i] = plain[i]^key[i];
      };
      write_text(0, 32, patient.patient_id, patient_id_key());
      write_text(32, 32, patient.first_name, first_name_key());
      write_text(64, 32, patient.last_name, last_name_key());
      std::vector<std::uint8_t> midkey(
        patient_mode_key().begin() + 96,
        patient_mode_key().begin() + 104
      );
      write_text(96, 8, patient.middle_name, midkey);
      const std::uint8_t sex = patient.sex == "M"?1:patient.sex == "F"?2:patient.sex == "O"?3:0;
      raw[104] = sex^kSexKey;
      if (!patient.birth_date.empty()) {
        unsigned y = 0, m = 0, d = 0;
        char a = 0, b = 0;
        std::istringstream is(patient.birth_date);
        is>>y>>a>>m>>b>>d;
        if (
          !is || a != '-' || b != '-' ||
          date_iso(
            static_cast<std::uint16_t>(y),
            static_cast<std::uint16_t>(m),
            static_cast<std::uint16_t>(d)
          ).empty()
        ) {
          throw std::runtime_error("birth_date must be valid YYYY-MM-DD");
        }
        std::array<std::uint8_t, 6> plain{};
        write_le<std::uint16_t>(plain.data(), static_cast<std::uint16_t>(y));
        write_le<std::uint16_t>(plain.data()+2, static_cast<std::uint16_t>(m));
        write_le<std::uint16_t>(plain.data()+4, static_cast<std::uint16_t>(d));
        const auto bk = hex_bytes(kBirthHex);
        for (std::size_t i = 0;i<6;++i) raw[105+i] = plain[i]^bk[i];
      }
      replacements_[idx].bytes = std::move(raw);
      patient_ = patient;
    }

    /// Replace raw OCT volume pixels and header dimensions.
    void FdaBackend::set_fda_raw_volume(
      std::size_t volume_index,
      const std::uint16_t* data,
      std::size_t slices,
      std::size_t height,
      std::size_t width
    ) {
      if (volume_index >= volumes_.size()) throw std::out_of_range("Volume index");
      const auto idx = find_chunk("@IMG_MOT_COMP_03");
      if (idx == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("IMG_MOT_COMP_03 not present");
      }
      auto old = chunk_bytes(idx);
      if (old.length<22) throw std::runtime_error("Truncated IMG_MOT_COMP_03");
      const auto* op = static_cast<const std::uint8_t*>(old.data);
      const std::size_t ow = read_le<std::uint32_t>(op + 1);
      const std::size_t oh = read_le<std::uint32_t>(op + 5);
      const std::size_t os = read_le<std::uint32_t>(op + 13);
      const std::size_t oldbytes = ow*oh*os*2u;
      const std::size_t trailer = (old.length >= 22+oldbytes)?old.length-22-oldbytes:0;
      const std::size_t newbytes = width*height*slices*2u;
      std::vector<std::uint8_t> out(22+newbytes+trailer);
      std::memcpy(out.data(), op, 22);
      out[0] = op[0];
      write_le<std::uint32_t>(out.data()+1, static_cast<std::uint32_t>(width));
      write_le<std::uint32_t>(out.data()+5, static_cast<std::uint32_t>(height));
      write_le<std::uint32_t>(out.data()+9, 16u);
      write_le<std::uint32_t>(out.data()+13, static_cast<std::uint32_t>(slices));
      write_le<std::uint32_t>(out.data()+18, static_cast<std::uint32_t>(newbytes));
      std::memcpy(out.data()+22, data, newbytes);
      if (trailer) std::memcpy(out.data()+22+newbytes, op+22+oldbytes, trailer);
      replacements_[idx].bytes = std::move(out);
    }

    /// Replace or append a layer segmentation.
    void FdaBackend::set_fda_segmentation(
      const std::string& layer,
      const std::uint16_t* data,
      std::size_t height,
      std::size_t width
    ) {
      const std::string id = native_layer(layer);
      std::size_t target = std::numeric_limits<std::size_t>::max();
      std::size_t trailer = 0;
      std::uint8_t method = 0, fmt = 0;
      for (std::size_t i = 0; i < raw_chunks_.size(); ++i) {
        if (raw_chunks_[i].native_name != "@CONTOUR_INFO") continue;
        auto v = chunk_bytes(i);
        if (v.length<34) continue;
        const auto* p = static_cast<const std::uint8_t*>(v.data);
        if (fixed_string(p, 20) == id) {
          target = i;
          method = p[20];
          fmt = p[21];
          const std::size_t ow = read_le<std::uint32_t>(p + 22);
          const std::size_t oh = read_le<std::uint32_t>(p + 26);
          const std::size_t oldbytes = ow*oh*2u;
          if (v.length >= 34+oldbytes) trailer = v.length-34-oldbytes;
          break;
        }
      }
      const std::size_t bytes = width*height*2u;
      std::vector<std::uint8_t> payload(34+bytes+trailer, 0);
      std::memcpy(payload.data(), id.data(), std::min<std::size_t>(20, id.size()));
      payload[20] = method;
      payload[21] = fmt;
      write_le<std::uint32_t>(payload.data()+22, static_cast<std::uint32_t>(width));
      write_le<std::uint32_t>(payload.data()+26, static_cast<std::uint32_t>(height));
      write_le<std::uint32_t>(payload.data()+30, static_cast<std::uint32_t>(bytes));
      std::memcpy(payload.data()+34, data, bytes);
      if (target != std::numeric_limits<std::size_t>::max()) {
        auto old = chunk_bytes(target);
        const auto* op = static_cast<const std::uint8_t*>(old.data);
        if (trailer) {
          const std::size_t ow = read_le<std::uint32_t>(op + 22);
          const std::size_t oh = read_le<std::uint32_t>(op + 26);
          std::memcpy(payload.data()+34+bytes, op+34+ow*oh*2u, trailer);
        }
        replacements_[target].bytes = std::move(payload);
        return;
      }
      ChunkInfo ci;
      ci.index = raw_chunks_.size();
      ci.name = "@CONTOUR_INFO";
      ci.payload_size = payload.size();
      raw_chunks_.push_back( {
        ci, "@CONTOUR_INFO"
      });
      chunks_.push_back(ci);
      replacements_[ci.index].bytes = std::move(payload);
    }

    /// Replace or append a JPEG fundus payload.
    void FdaBackend::set_fda_fundus_jpeg(
      const std::uint8_t* jpeg,
      std::size_t size,
      std::size_t height,
      std::size_t width,
      std::size_t channels
    ) {
      std::size_t idx = find_chunk("@IMG_FUNDUS");
      std::vector<std::uint8_t> payload(24+size, 0);
      write_le<std::uint32_t>(payload.data(), static_cast<std::uint32_t>(width));
      write_le<std::uint32_t>(payload.data()+4, static_cast<std::uint32_t>(height));
      write_le<std::uint32_t>(payload.data()+8, static_cast<std::uint32_t>(channels*8));
      write_le<std::uint32_t>(payload.data()+12, 1u);
      std::memcpy(payload.data()+16, "JPEG", 4);
      write_le<std::uint32_t>(payload.data()+20, static_cast<std::uint32_t>(size));
      std::memcpy(payload.data()+24, jpeg, size);
      if (idx == std::numeric_limits<std::size_t>::max()) {
        ChunkInfo ci;
        ci.index = raw_chunks_.size();
        ci.name = "@IMG_FUNDUS";
        ci.payload_size = payload.size();
        raw_chunks_.push_back( {
          ci, "@IMG_FUNDUS"
        });
        chunks_.push_back(ci);
        idx = ci.index;
      }
      replacements_[idx].bytes = std::move(payload);
    }

    /// Rebuild FDA chunk stream preserving unknown payloads and chunk order.
    void FdaBackend::save(const std::string& output_path) const {
      std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
      if (!out) {
        throw std::runtime_error("Cannot create output: " + output_path);
      }
      out.write(reinterpret_cast<const char*>(header_.data()), header_.size());
      for (std::size_t i = 0; i < raw_chunks_.size(); ++i) {
        const auto& c = raw_chunks_[i];
        if (c.native_name.size() > 255) {
          throw std::runtime_error("FDA chunk name too long");
        }
        const std::uint8_t n = static_cast<std::uint8_t>(c.native_name.size());
        out.write(reinterpret_cast<const char*>(&n), 1);
        out.write(c.native_name.data(), static_cast<std::streamsize>(c.native_name.size()));
        auto it = replacements_.find(i);
        const std::uint32_t sz = it == replacements_.end()
          ? static_cast<std::uint32_t>(c.info.payload_size)
          : static_cast<std::uint32_t>(it->second.bytes.size());
        std::array<std::uint8_t, 4> b{};
        write_le<std::uint32_t>(b.data(), sz);
        out.write(reinterpret_cast<const char*>(b.data()), 4);
        if (it == replacements_.end()) {
          const auto* p = checked_span(file_, c.info.payload_offset, c.info.payload_size);
          out.write(
            reinterpret_cast<const char*>(p),
            static_cast<std::streamsize>(c.info.payload_size)
          );
        } else if (!it->second.bytes.empty()) {
          out.write(
            reinterpret_cast<const char*>(it->second.bytes.data()),
            static_cast<std::streamsize>(it->second.bytes.size())
          );
        }
      }
      const std::uint8_t zero = 0;
      out.write(reinterpret_cast<const char*>(&zero), 1);
      if (!out) {
        throw std::runtime_error("Failed while writing FDA output");
      }
    }

    /// Copy one A-scan to float32.
    void FdaBackend::copy_a_scan_f32(
      std::size_t vi,
      std::size_t si,
      std::size_t xi,
      float* out,
      std::size_t n,
      bool
    ) const {
      if (vi >= volumes_.size() || si >= volumes_[vi].slices.size()) {
        throw std::out_of_range("Volume/slice index");
      }
      const auto& a = volumes_[vi].slices[si].pixels;
      if (!a.data) {
        throw std::runtime_error(
          "A-scan copy requires a raw FDA volume; decode encoded B-scan first"
        );
      }
      if (xi >= a.width || n < a.height) {
        throw std::runtime_error(
          "A-scan output buffer too small or x out of range"
        );
      }
      const auto* base = static_cast<const std::uint8_t*>(a.data);
      for (std::size_t y = 0; y < a.height; ++y) {
        out[y] = static_cast<float>(read_le<std::uint16_t>(
          base + y * a.row_stride_bytes + xi * 2u
        ));
      }
    }

    /// Copy one B-scan to float32.
    void FdaBackend::copy_b_scan_f32(
      std::size_t vi,
      std::size_t si,
      float* out,
      std::size_t n,
      bool
    ) const {
      if (vi >= volumes_.size() || si >= volumes_[vi].slices.size()) {
        throw std::out_of_range("Volume/slice index");
      }
      const auto& a = volumes_[vi].slices[si].pixels;
      if (!a.data) {
        throw std::runtime_error(
          "B-scan copy requires a raw FDA volume; decode encoded B-scan first"
        );
      }
      if (n < a.height * a.width) {
        throw std::runtime_error("B-scan output buffer too small");
      }
      const auto* base = static_cast<const std::uint8_t*>(a.data);
      for (std::size_t y = 0; y < a.height; ++y) {
        for (std::size_t x = 0; x < a.width; ++x) {
          out[y * a.width + x] = static_cast<float>(read_le<std::uint16_t>(
            base + y * a.row_stride_bytes + x * 2u
          ));
        }
      }
    }

  }
  // namespace

  /// Construct a Topcon FDA backend.
  std::unique_ptr<Backend> make_fda_backend(const std::string& path) {
    return std::make_unique<FdaBackend>(path);
  }

}
// namespace octio
