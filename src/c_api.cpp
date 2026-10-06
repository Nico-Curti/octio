#include "octio/c_api.h"
#include "octio/document.hpp"
#include "internal.hpp"

#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct octio_handle {
  std::unique_ptr<octio::Document> doc;
  std::string buffer;
};

namespace {

thread_local std::string g_error;

/// Execute a C API operation and convert C++ exceptions to an integer status.
template <typename F>
int guard(F&& operation) noexcept {
  try {
    operation();
    g_error.clear();
    return 0;
  } catch (const std::exception& error) {
    g_error = error.what();
    return -1;
  } catch (...) {
    g_error = "Unknown C++ exception";
    return -1;
  }
}

/// Convert an element type enum to the public C integer code.
int dtype_code(octio::DType type) noexcept {
  return static_cast<int>(type);
}

/// Serialize all common metadata to compact JSON.
std::string metadata_json(const octio::Document& document) {
  const auto& patient = document.patient();
  const auto& acquisition = document.acquisition();
  const auto& device = document.device();

  std::ostringstream stream;
  stream << '{';

  auto field = [&](const char* key, const std::string& value, bool comma = true) {
    stream << '"' << key << "\":\"" << octio::json_escape(value) << '"';
    if (comma) {
      stream << ',';
    }
  };

  stream << "\"format\":\""
        << (document.format() == octio::Format::FDA ? "FDA" : "E2E")
        << "\",";

  stream << "\"patient\":{";
  field("patient_id", patient.patient_id);
  field("first_name", patient.first_name);
  field("middle_name", patient.middle_name);
  field("last_name", patient.last_name);
  field("sex", patient.sex);
  field("birth_date", patient.birth_date);
  field("physician", patient.physician);
  field("zip_code", patient.zip_code);
  field("address", patient.address);
  field("phones", patient.phones);
  field("description", patient.description, false);
  stream << "},";

  stream << "\"acquisition\":{";
  field("datetime", acquisition.datetime);
  field("laterality", acquisition.laterality);
  stream << "\"session_id\":" << acquisition.session_id << ',';
  stream << "\"scan_mode\":" << acquisition.scan_mode << ',';
  field("label", acquisition.label, false);
  stream << "},";

  stream << "\"device\":{";
  field("model", device.model);
  field("serial", device.serial);
  field("spectrometer_serial", device.spectrometer_serial);
  field("rom_version", device.rom_version);
  field("software_name", device.software_name);
  field("software_version", device.software_version, false);
  stream << "},";

  stream << "\"metadata\":{";
  bool first = true;
  for (const auto& item : document.metadata()) {
    if (!first) {
      stream << ',';
    }
    first = false;
    stream << '"' << octio::json_escape(item.first) << "\":\""
          << octio::json_escape(item.second) << '"';
  }
  stream << "}}";

  return stream.str();
}

/// Serialize native chunk descriptors to compact JSON.
std::string chunks_json(const octio::Document& document) {
  std::ostringstream stream;
  stream << '[';

  bool first = true;
  for (const auto& chunk : document.chunks()) {
    if (!first) {
      stream << ',';
    }
    first = false;

    stream << "{\"index\":" << chunk.index
          << ",\"name\":\"" << octio::json_escape(chunk.name)
          << "\",\"offset\":" << chunk.payload_offset
          << ",\"size\":" << chunk.payload_size
          << ",\"type\":" << chunk.type
          << ",\"slice_id\":" << chunk.slice_id
          << ",\"indicator\":" << chunk.indicator
          << ",\"patient_db_id\":" << chunk.patient_db_id
          << ",\"study_id\":" << chunk.study_id
          << ",\"series_id\":" << chunk.series_id << '}';
  }

  stream << ']';
  return stream.str();
}

}  // namespace

extern "C" {

/// Open an OCT file and return an opaque handle, or NULL on failure.
octio_handle* octio_open(const char* path) {
  try {
    if (path == nullptr) {
      throw std::runtime_error("path is null");
    }

    auto* handle = new octio_handle;
    handle->doc = octio::Document::open(path);
    g_error.clear();
    return handle;
  } catch (const std::exception& error) {
    g_error = error.what();
    return nullptr;
  } catch (...) {
    g_error = "Unknown C++ exception";
    return nullptr;
  }
}

/// Close a handle returned by octio_open.
void octio_close(octio_handle* handle) {
  delete handle;
}

/// Return the last thread-local error string.
const char* octio_last_error(void) {
  return g_error.c_str();
}

/// Return the native format code.
int octio_format(const octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    return 0;
  }
  return static_cast<int>(handle->doc->format());
}

/// Return metadata as UTF-8 JSON in an internal stable buffer.
const char* octio_metadata_json(octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    g_error = "Invalid handle";
    return nullptr;
  }

  try {
    handle->buffer = metadata_json(*handle->doc);
    g_error.clear();
    return handle->buffer.c_str();
  } catch (const std::exception& error) {
    g_error = error.what();
    return nullptr;
  }
}

/// Return native chunk descriptors as UTF-8 JSON.
const char* octio_chunks_json(octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    g_error = "Invalid handle";
    return nullptr;
  }

  try {
    handle->buffer = chunks_json(*handle->doc);
    g_error.clear();
    return handle->buffer.c_str();
  } catch (const std::exception& error) {
    g_error = error.what();
    return nullptr;
  }
}

/// Return the number of OCT volumes.
size_t octio_volume_count(const octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    return 0;
  }
  return handle->doc->volumes().size();
}

/// Return a stable UTF-8 volume identifier.
const char* octio_volume_id(octio_handle* handle, size_t index) {
  if (handle == nullptr || handle->doc == nullptr ||
      index >= handle->doc->volumes().size()) {
    g_error = "Volume index out of range";
    return nullptr;
  }

  handle->buffer = handle->doc->volumes()[index].id;
  return handle->buffer.c_str();
}

/// Return non-zero when a volume stores Heidelberg custom ufloat16 codes.
int octio_volume_is_e2e_custom_float(const octio_handle* handle, size_t index) {
  if (handle == nullptr || handle->doc == nullptr ||
      index >= handle->doc->volumes().size()) {
    return 0;
  }
  return handle->doc->volumes()[index].e2e_custom_float ? 1 : 0;
}

/// Return the number of B-scans in a volume.
size_t octio_volume_slice_count(const octio_handle* handle, size_t index) {
  if (handle == nullptr || handle->doc == nullptr ||
      index >= handle->doc->volumes().size()) {
    return 0;
  }
  return handle->doc->volumes()[index].slices.size();
}

/// Return a zero-copy B-scan view.
int octio_volume_slice(
  const octio_handle* handle,
  size_t volume_index,
  size_t slice_index,
  octio_array2d_info* out
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || out == nullptr) {
      throw std::runtime_error("Invalid argument");
    }

    const auto& volumes = handle->doc->volumes();
    if (volume_index >= volumes.size() ||
        slice_index >= volumes[volume_index].slices.size()) {
      throw std::out_of_range("Volume/slice index");
    }

    const auto& array = volumes[volume_index].slices[slice_index].pixels;
    if (array.data == nullptr) {
      throw std::runtime_error(
        "B-scan is encoded; use octio_volume_slice_encoded"
      );
    }

    out->data = array.data;
    out->height = array.height;
    out->width = array.width;
    out->row_stride_bytes = array.row_stride_bytes;
    out->dtype = dtype_code(array.dtype);
  });
}

/// Return an encoded B-scan payload when the native volume is JPEG-compressed.
int octio_volume_slice_encoded(
  const octio_handle* handle,
  size_t volume_index,
  size_t slice_index,
  const void** data,
  size_t* size,
  size_t* height,
  size_t* width,
  int* encoding
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || data == nullptr ||
        size == nullptr || height == nullptr || width == nullptr ||
        encoding == nullptr) {
      throw std::runtime_error("Invalid argument");
    }

    const auto& volumes = handle->doc->volumes();
    if (volume_index >= volumes.size() ||
        slice_index >= volumes[volume_index].slices.size()) {
      throw std::out_of_range("Volume/slice index");
    }

    const auto& bscan = volumes[volume_index].slices[slice_index];
    if (bscan.encoded_data == nullptr || bscan.encoded_size == 0) {
      throw std::runtime_error("B-scan is not stored as an encoded image");
    }

    *data = bscan.encoded_data;
    *size = bscan.encoded_size;
    *height = bscan.encoded_height;
    *width = bscan.encoded_width;
    *encoding = static_cast<int>(bscan.encoding);
  });
}

/// Copy one B-scan to float32.
int octio_copy_bscan_f32(
  const octio_handle* handle,
  size_t volume_index,
  size_t slice_index,
  float* out,
  size_t count,
  int decode
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || out == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->copy_b_scan_f32(
      volume_index,
      slice_index,
      out,
      count,
      decode != 0
    );
  });
}

/// Copy one A-scan to float32.
int octio_copy_ascan_f32(
  const octio_handle* handle,
  size_t volume_index,
  size_t slice_index,
  size_t x_index,
  float* out,
  size_t count,
  int decode
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || out == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->copy_a_scan_f32(
      volume_index,
      slice_index,
      x_index,
      out,
      count,
      decode != 0
    );
  });
}

/// Return the number of fundus/localizer images.
size_t octio_fundus_count(const octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    return 0;
  }
  return handle->doc->fundus_images().size();
}

/// Return a fundus/localizer data view.
int octio_fundus(
  const octio_handle* handle,
  size_t index,
  octio_fundus_info* out
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || out == nullptr) {
      throw std::runtime_error("Invalid argument");
    }

    const auto& images = handle->doc->fundus_images();
    if (index >= images.size()) {
      throw std::out_of_range("Fundus index");
    }

    const auto& image = images[index];
    out->data = image.data;
    out->byte_size = image.byte_size;
    out->height = image.height;
    out->width = image.width;
    out->channels = image.channels;
    out->dtype = dtype_code(image.dtype);
    out->encoding = static_cast<int>(image.encoding);
  });
}

/// Return the number of segmentation arrays.
size_t octio_segmentation_count(const octio_handle* handle) {
  if (handle == nullptr || handle->doc == nullptr) {
    return 0;
  }
  return handle->doc->segmentations().size();
}

/// Return one segmentation view.
int octio_segmentation(
  const octio_handle* handle,
  size_t index,
  octio_segmentation_info* out
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || out == nullptr) {
      throw std::runtime_error("Invalid argument");
    }

    const auto& segmentations = handle->doc->segmentations();
    if (index >= segmentations.size()) {
      throw std::out_of_range("Segmentation index");
    }

    const auto& segmentation = segmentations[index];
    out->data = segmentation.values.data;
    out->height = segmentation.values.height;
    out->width = segmentation.values.width;
    out->row_stride_bytes = segmentation.values.row_stride_bytes;
    out->dtype = dtype_code(segmentation.values.dtype);
    out->layer_id = segmentation.layer_id;
    out->slice_index = segmentation.slice_index;
    out->measured_from_bottom = segmentation.measured_from_bottom ? 1 : 0;
  });
}

/// Return a segmentation layer name in an internal UTF-8 buffer.
const char* octio_segmentation_name(octio_handle* handle, size_t index) {
  if (handle == nullptr || handle->doc == nullptr ||
      index >= handle->doc->segmentations().size()) {
    g_error = "Segmentation index out of range";
    return nullptr;
  }

  handle->buffer = handle->doc->segmentations()[index].layer;
  return handle->buffer.c_str();
}

/// Return a raw native chunk as a zero-copy byte span.
int octio_chunk_bytes(
  const octio_handle* handle,
  size_t index,
  const void** data,
  size_t* size
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || data == nullptr ||
        size == nullptr) {
      throw std::runtime_error("Invalid argument");
    }

    const auto view = handle->doc->chunk_bytes(index);
    *data = view.data;
    *size = view.length;
  });
}

/// Replace a native chunk payload.
int octio_replace_chunk(
  octio_handle* handle,
  size_t index,
  const void* data,
  size_t size
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr ||
        (size != 0 && data == nullptr)) {
      throw std::runtime_error("Invalid argument");
    }

    const auto* source = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> bytes;
    if (size != 0) {
      bytes.assign(source, source + size);
    }
    handle->doc->replace_chunk(index, bytes);
  });
}

/// Update common patient fields.
int octio_set_patient(
  octio_handle* handle,
  const char* patient_id,
  const char* first_name,
  const char* middle_name,
  const char* last_name,
  const char* sex,
  const char* birth_date
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr) {
      throw std::runtime_error("Invalid handle");
    }

    auto patient = handle->doc->patient();
    if (patient_id != nullptr) {
      patient.patient_id = patient_id;
    }
    if (first_name != nullptr) {
      patient.first_name = first_name;
    }
    if (middle_name != nullptr) {
      patient.middle_name = middle_name;
    }
    if (last_name != nullptr) {
      patient.last_name = last_name;
    }
    if (sex != nullptr) {
      patient.sex = sex;
    }
    if (birth_date != nullptr) {
      patient.birth_date = birth_date;
    }
    handle->doc->set_patient(patient);
  });
}

/// Replace an FDA raw uint16 OCT volume.
int octio_set_fda_raw_volume(
  octio_handle* handle,
  size_t volume_index,
  const uint16_t* data,
  size_t slices,
  size_t height,
  size_t width
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || data == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->set_fda_raw_volume(
      volume_index,
      data,
      slices,
      height,
      width
    );
  });
}

/// Replace or append an FDA segmentation layer.
int octio_set_fda_segmentation(
  octio_handle* handle,
  const char* layer,
  const uint16_t* data,
  size_t height,
  size_t width
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || layer == nullptr ||
        data == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->set_fda_segmentation(layer, data, height, width);
  });
}

/// Replace or append an FDA JPEG fundus image.
int octio_set_fda_fundus_jpeg(
  octio_handle* handle,
  const uint8_t* jpeg,
  size_t size,
  size_t height,
  size_t width,
  size_t channels
) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || jpeg == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->set_fda_fundus_jpeg(
      jpeg,
      size,
      height,
      width,
      channels
    );
  });
}

/// Save the document to a new file.
int octio_save(const octio_handle* handle, const char* output_path) {
  return guard([&] {
    if (handle == nullptr || handle->doc == nullptr || output_path == nullptr) {
      throw std::runtime_error("Invalid argument");
    }
    handle->doc->save(output_path);
  });
}

}  // extern "C"
