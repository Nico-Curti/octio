#include "octio/document.hpp"

#include <iostream>
#include <string>

namespace {

/** \brief Convert a volume's first B-scan representation to a stable label. */
std::string storage_name(const octio::VolumeView& volume) {
  if (volume.slices.empty()) {
    return "empty";
  }
  const auto& slice = volume.slices.front();
  if (slice.pixels.data != nullptr) {
    return "raw";
  }
  if (slice.encoding == octio::ImageEncoding::JPEG) {
    return "jpeg";
  }
  return "encoded";
}

/** \brief Print the canonical summary also emitted by examples/read_oct.py. */
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

/** \brief Open one FDA/E2E file and print the unified metadata summary. */
int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: read_oct <file.fda|file.e2e>\n";
    return 2;
  }

  try {
    auto document = octio::Document::open(argv[1]);
    print_summary(*document);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "read_oct: " << error.what() << '\n';
    return 1;
  }
}
