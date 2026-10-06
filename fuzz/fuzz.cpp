#include "minivdb.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

int main(int, char** argv) {
  // Keep the scratch file beside this executable, inside the build directory.
  const auto path = std::filesystem::path(argv[0]).parent_path() / "fuzz-input.bin";

  // Start with a real, valid index so changed bytes reach useful loader code.
  minivdb::Index original({2, minivdb::Metric::l2});
  original.add(1, std::vector<float>{1, 0});
  original.add(2, std::vector<float>{0, 1});
  original.save(path);
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read seed index");
  const std::vector<char> saved{std::istreambuf_iterator<char>(input), {}};
  input.close();

  std::mt19937 random(42);
  int rejected = 0;
  for (int test = 0; test < 1000; ++test) {
    // Make a fresh copy and flip one randomly chosen bit.
    auto changed = saved;
    changed[random() % changed.size()] ^= static_cast<char>(1U << (random() % 8));
    {
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output.exceptions(std::ios::failbit | std::ios::badbit);
      output.write(changed.data(), static_cast<std::streamsize>(changed.size()));
      output.close();
    }
    try {
      const auto index = minivdb::Index::load(path);
      if (index.dimensions() == 2) index.search(std::vector<float>{1, 0}, 2);
    } catch (const std::invalid_argument&) {
      ++rejected;  // Rejecting a malformed file is expected.
    } catch (const std::runtime_error&) {
      ++rejected;
    }
    // A memory error or undefined behavior makes the sanitizer stop this run.
  }
  std::filesystem::remove(path);
  std::cout << "1000 mutations completed; " << rejected << " files rejected\n";
}
