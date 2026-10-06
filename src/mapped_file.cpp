#include "internal.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace octio {

  /// Open and memory-map a file read-only.
  MappedFile::MappedFile(const std::string& path) : path_(path) {
#if defined(_WIN32)
    HANDLE f = CreateFileA(
      path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
    if (f == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("Cannot open file: " + path);
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0) {
      CloseHandle(f);
      throw std::runtime_error("Cannot determine file size: " + path);
    }
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) {
      CloseHandle(f);
      throw std::runtime_error("CreateFileMapping failed: " + path);
    }
    auto* p = static_cast<const std::uint8_t*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
    if (!p) {
      CloseHandle(m);
      CloseHandle(f);
      throw std::runtime_error("MapViewOfFile failed: " + path);
    }
    file_ = f;
    mapping_ = m;
    data_ = p;
    size_ = static_cast<std::size_t>(sz.QuadPart);
#else
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) {
      throw std::runtime_error("Cannot open file: " + path);
    }
    struct stat st{};
    if (fstat(fd_, &st) != 0 || st.st_size <= 0) {
      ::close(fd_);
      fd_ = -1;
      throw std::runtime_error("Cannot determine file size: " + path);
    }
    void* p = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_SHARED, fd_, 0);
    if (p == MAP_FAILED) {
      ::close(fd_);
      fd_ = -1;
      throw std::runtime_error("mmap failed: " + path);
    }
    data_ = static_cast<const std::uint8_t*>(p);
    size_ = static_cast<std::size_t>(st.st_size);
#endif
  }

  /// Release the mapping and native file handles.
  MappedFile::~MappedFile() {
#if defined(_WIN32)
    if (data_) {
      UnmapViewOfFile(data_);
    }
    if (mapping_) {
      CloseHandle(static_cast<HANDLE>(mapping_));
    }
    if (file_) {
      CloseHandle(static_cast<HANDLE>(file_));
    }
#else
    if (data_) {
      munmap(const_cast<std::uint8_t*>(data_), size_);
    }
    if (fd_ >= 0) {
      ::close(fd_);
    }
#endif
  }

  /// Move-construct a mapped file.
  MappedFile::MappedFile(MappedFile && other) noexcept {
    *this = std::move(other);
  }

  /// Move-assign a mapped file.
  MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this == &other) {
      return *this;
    }

#if defined(_WIN32)
    if (data_) {
      UnmapViewOfFile(data_);
    }
    if (mapping_) {
      CloseHandle(static_cast<HANDLE>(mapping_));
    }
    if (file_) {
      CloseHandle(static_cast<HANDLE>(file_));
    }
#else
    if (data_) {
      munmap(const_cast<std::uint8_t*>(data_), size_);
    }
    if (fd_ >= 0) {
      ::close(fd_);
    }
#endif

    path_ = std::move(other.path_);
    data_ = other.data_;
    size_ = other.size_;
#if defined(_WIN32)
    file_ = other.file_;
    mapping_ = other.mapping_;
    other.file_ = nullptr;
    other.mapping_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
    return *this;
  }

  /// Read a little-endian float32 without alignment assumptions.
  float read_f32_le(const std::uint8_t* p) noexcept {
    std::uint32_t u = read_le<std::uint32_t>(p);
    float v = 0.0f;
    std::memcpy(&v, &u, sizeof(v));
    return v;
  }

  /// Read a little-endian float64 without alignment assumptions.
  double read_f64_le(const std::uint8_t* p) noexcept {
    std::uint64_t u = read_le<std::uint64_t>(p);
    double v = 0.0;
    std::memcpy(&v, &u, sizeof(v));
    return v;
  }

  /// Write a little-endian float32 without alignment assumptions.
  void write_f32_le(std::uint8_t* p, float v) noexcept {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(v));
    write_le<std::uint32_t>(p, u);
  }

  /// Convert a fixed-width C-style byte string to trimmed std::string.
  std::string fixed_string(const std::uint8_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != 0) {
      ++len;
    }
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\0')) {
      --len;
    }
    return std::string(reinterpret_cast<const char*>(p), len);
  }

  /// Escape a UTF-8/ASCII string for JSON output.
  std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c : s) {
      switch (c) {
        case '"':
          o << "\\\"";
          break;
        case '\\':
          o << "\\\\";
          break;
        case '\b':
          o << "\\b";
          break;
        case '\f':
          o << "\\f";
          break;
        case '\n':
          o << "\\n";
          break;
        case '\r':
          o << "\\r";
          break;
        case '\t':
          o << "\\t";
          break;
        default:
          if (c < 0x20) {
          const char* hex = "0123456789abcdef";
          o << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
          } else {
            o << static_cast<char>(c);
          }
      }
    }
    return o.str();
  }

  /// Validate an in-range mapped span and return its start pointer.
  const std::uint8_t* checked_span(
    const MappedFile& file,
    std::uint64_t offset,
    std::uint64_t size
  ) {
    if (offset > file.size() || size > file.size() - static_cast<std::size_t>(offset)) {
      throw std::runtime_error("Mapped span exceeds file bounds");
    }
    return file.data() + static_cast<std::size_t>(offset);
  }

}
// namespace octio
