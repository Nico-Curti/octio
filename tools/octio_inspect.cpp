#include "octio/document.hpp"

#include <iostream>
#include <string>

namespace {

/** \brief Return a stable storage label for a logical volume. */
std::string storage_name(const octio::VolumeView& volume) {
  if (volume.slices.empty()) {
    return "empty";
  }
  const auto& slice = volume.slices.front();
  if (slice.pixels.data != nullptr) {
    return "raw";
  }
  return slice.encoding == octio::ImageEncoding::JPEG ? "jpeg" : "encoded";
}

/** \brief Print the same canonical summary used by the language examples. */
void print_summary(const octio::Document& document) {
  const auto& patient = document.patient();
  const auto& acquisition = document.acquisition();
  const auto& volumes = document.volumes();

  std::cout << "Format: "
            << (document.format() == octio::Format::FDA ? "FDA" : "E2E")
            << '\n';
  std::string patient_name = patient.first_name;
  if (!patient_name.empty() && !patient.last_name.empty()) {
    patient_name += ' ';
  }
  patient_name += patient.last_name;
  std::cout << "Patient:";
  if (!patient_name.empty()) {
    std::cout << ' ' << patient_name;
  }
  std::cout << '\n';
  std::cout << "Patient ID: " << patient.patient_id << '\n';
  std::cout << "DOB: " << patient.birth_date << '\n';
  std::cout << "Laterality: " << acquisition.laterality << '\n';
  std::cout << "Volumes: " << volumes.size() << '\n';

  for (std::size_t index = 0; index < volumes.size(); ++index) {
    const auto& volume = volumes[index];
    std::size_t height = 0;
    std::size_t width = 0;
    if (!volume.slices.empty()) {
      const auto& slice = volume.slices.front();
      height = slice.pixels.data != nullptr ? slice.pixels.height : slice.encoded_height;
      width = slice.pixels.data != nullptr ? slice.pixels.width : slice.encoded_width;
    }
    std::cout << "  [" << index << "] " << volume.id
              << " slices=" << volume.slices.size()
              << " shape=" << height << 'x' << width
              << " storage=" << storage_name(volume) << '\n';
  }

  std::cout << "Fundus/localizers: " << document.fundus_images().size() << '\n';
  std::cout << "Segmentations: " << document.segmentations().size() << '\n';
  std::cout << "Native chunks: " << document.chunks().size() << '\n';
}

}  // namespace

/** \brief Inspect a single FDA or E2E file from the native command line. */
int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: octio_inspect <file.fda|file.e2e>\n";
    return 2;
  }

  try {
    auto document = octio::Document::open(argv[1]);
    print_summary(*document);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "octio_inspect: " << error.what() << '\n';
    return 1;
  }
}
