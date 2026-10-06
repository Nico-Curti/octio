#include "octio/document.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

  /** Write one little-endian uint32 to an output stream. */
  void u32(std::ofstream& f, std::uint32_t v) {
    char b[4] = {
      static_cast<char>(v),
      static_cast<char>(v >> 8),
      static_cast<char>(v >> 16),
      static_cast<char>(v >> 24)
    };
    f.write(b, 4);
  }

  /** Write one length-prefixed FDA chunk. */
  void fda_chunk(
    std::ofstream& f,
    const std::string& name,
    const std::vector<std::uint8_t>& payload
  ) {
    const auto n = static_cast<std::uint8_t>(name.size());
    f.write(reinterpret_cast<const char*>(&n), 1);
    f.write(name.data(), static_cast<std::streamsize>(name.size()));
    u32(f, static_cast<std::uint32_t>(payload.size()));
    f.write(
      reinterpret_cast<const char*>(payload.data()),
      static_cast<std::streamsize>(payload.size())
    );
  }

  /** Build a small synthetic FDA file containing one raw volume and one contour. */
  std::filesystem::path make_fda(const std::filesystem::path& dir) {
    auto p = dir / "synthetic.fda";
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const char header[15] = {
      'F', 'O', 'C', 'T', 'F', 'D', 'A', 2, 0, 0, 0, 0, 0, 0, 0
    };
    f.write(header, 15);
    std::vector<std::uint8_t> vol(22 + 2 * 3 * 2 * 2 + 4, 0);
    vol[0] = 0;
    auto w32 = [&](std::size_t off, std::uint32_t v) {
      for (int i = 0;i<4;++i)vol[off+i] = static_cast<std::uint8_t>(v>>(8*i));
    };
    w32(1, 2);
    w32(5, 3);
    w32(9, 16);
    w32(13, 2);
    vol[17] = 0;
    w32(18, 24);
    for (std::uint16_t i = 0;i<12;++i) {
      vol[22+i*2] = static_cast<std::uint8_t>(i);
      vol[23+i*2] = 0;
    }
    vol[vol.size()-4] = 0xde;
    vol[vol.size()-3] = 0xad;
    vol[vol.size()-2] = 0xbe;
    vol[vol.size()-1] = 0xef;
    fda_chunk(f, "@IMG_MOT_COMP_03", vol);
    std::vector<std::uint8_t> seg(34 + 2*2*2 + 3, 0);
    std::memcpy(seg.data(), "MULTILAYERS_1", 13);
    seg[22] = 2;
    seg[26] = 2;
    seg[30] = 8;
    for (std::uint16_t i = 0;i<4;++i) {
      seg[34+i*2] = static_cast<std::uint8_t>(i+10);
      seg[35+i*2] = 0;
    }
    seg[seg.size()-3] = 1;
    seg[seg.size()-2] = 2;
    seg[seg.size()-1] = 3;
    fda_chunk(f, "@CONTOUR_INFO", seg);
    const std::uint8_t zero = 0;
    f.write(reinterpret_cast<const char*>(&zero), 1);
    return p;
  }

  /** Build a tiny synthetic single-volume E2E file with one OCT slice and one contour. */
  std::filesystem::path make_e2e(const std::filesystem::path& dir) {
    auto p = dir/"synthetic.e2e";
    std::vector<std::uint8_t> b(512, 0);
    auto w16 = [&](std::size_t off, std::uint16_t v) {
      b[off] = v&255;
      b[off+1] = (v>>8)&255;
    };
    auto w32 = [&](std::size_t off, std::uint32_t v) {
      for (int i = 0;i<4;++i)b[off+i] = (v>>(8*i))&255;
    };
    std::memcpy(b.data(), "E2EFORMAT", 9);
    w32(12, 1);
    const std::size_t d = 36;
    std::memcpy(b.data()+d, "DIR", 3);
    w32(d+36, 2);
    w32(d+40, 0);
    const std::size_t s0 = d+52, s1 = s0+44;
    w32(s0, 100);
    w32(s0+4, 200);
    w32(s0+8, 32);
    w32(s0+16, 1);
    w32(s0+20, 2);
    w32(s0+24, 3);
    w32(s0+28, 0);
    w32(s0+36, 1073741824u);
    w32(s1, 101);
    w32(s1+4, 320);
    w32(s1+8, 44);
    w32(s1+16, 1);
    w32(s1+20, 2);
    w32(s1+24, 3);
    w32(s1+28, 0);
    w32(s1+36, 0x2723u);
    const std::size_t c0 = 200;
    std::memcpy(b.data()+c0, "CHUNK", 5);
    w32(c0+24, 32);
    w32(c0+32, 1);
    w32(c0+36, 2);
    w32(c0+40, 3);
    w32(c0+44, 0);
    w16(c0+48, 1);
    w32(c0+52, 1073741824u);
    const std::size_t p0 = c0+60;
    w32(p0, 0);
    w32(p0+4, 0);
    w32(p0+8, 0);
    w32(p0+12, 2);
    w32(p0+16, 3);
    for (std::uint16_t i = 0;i<6;++i) {
      b[p0+20+i*2] = i;
      b[p0+21+i*2] = 0;
    }
    const std::size_t c1 = 320;
    std::memcpy(b.data()+c1, "CHUNK", 5);
    w32(c1+24, 44);
    w32(c1+32, 1);
    w32(c1+36, 2);
    w32(c1+40, 3);
    w32(c1+44, 0);
    w16(c1+48, 0);
    w32(c1+52, 0x2723u);
    const std::size_t p1 = c1+60;
    w32(p1+4, 7);
    w32(p1+12, 2);
    float vals[2] = {
      1.5f, 2.5f
    };
    std::memcpy(b.data()+p1+36, vals, sizeof(vals));
    b.resize(c1+60+44);
    std::ofstream f(p, std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    return p;
  }

  /** Compare two files byte-for-byte. */
  bool identical(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    std::vector<char> bx((std::istreambuf_iterator<char>(x)), {});
    std::vector<char> by((std::istreambuf_iterator<char>(y)), {});
    return bx == by;
  }
}

/** Run synthetic FDA/E2E parsing and lossless round-trip tests. */
int main() {
  const auto dir = std::filesystem::temp_directory_path()/"octio_tests";
  std::filesystem::create_directories(dir);
  const auto fda = make_fda(dir);
  auto d = octio::Document::open(fda.string());
  assert(d->format() == octio::Format::FDA);
  assert(d->volumes().size() == 1);
  assert(d->volumes()[0].slices.size() == 2);
  assert(d->volumes()[0].slices[0].pixels.height == 3);
  assert(d->volumes()[0].slices[0].pixels.width == 2);
  assert(d->segmentations().size() == 1);
  assert(d->segmentations()[0].layer == "ILM");
  std::vector<float>a(3);
  d->copy_a_scan_f32(0, 0, 1, a.data(), a.size(), true);
  assert(a[0] == 1.0f && a[1] == 3.0f && a[2] == 5.0f);
  const auto fda2 = dir/"roundtrip.fda";
  d->save(fda2.string());
  assert(identical(fda, fda2));
  std::vector<std::uint16_t> changed_volume(12);
  for (std::size_t i = 0; i < changed_volume.size(); ++i) {
    changed_volume[i] = static_cast<std::uint16_t>(100 + i);
  }
  d->set_fda_raw_volume(0, changed_volume.data(), 2, 3, 2);
  std::vector<std::uint16_t> changed_seg = {
    50, 51, 52, 53
  };
  d->set_fda_segmentation("ILM", changed_seg.data(), 2, 2);
  const auto fda3 = dir/"mutated.fda";
  d->save(fda3.string());
  auto dm = octio::Document::open(fda3.string());
  assert(dm->volumes().size() == 1);
  assert(*static_cast<const std::uint16_t*>(dm->volumes()[0].slices[0].pixels.data) == 100);
  assert(dm->segmentations().size() == 1);
  assert(*static_cast<const std::uint16_t*>(dm->segmentations()[0].values.data) == 50);
  const auto e2e = make_e2e(dir);
  auto e = octio::Document::open(e2e.string());
  assert(e->format() == octio::Format::E2E);
  assert(e->volumes().size() == 1);
  assert(e->volumes()[0].slices.size() == 1);
  assert(e->segmentations().size() == 1);
  const auto e2e2 = dir/"roundtrip.e2e";
  e->save(e2e2.string());
  assert(identical(e2e, e2e2));
  std::cout<<"All octio tests passed\n";
  return 0;
}
