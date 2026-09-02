#include "minivdb.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
[[noreturn]] void usage() {
  throw std::invalid_argument(
      "usage:\n  minivdb build INDEX DIM METRIC INPUT [M EF_CONSTRUCTION]\n"
      "  minivdb add INDEX ID VALUES...\n  minivdb query INDEX K EF VALUES...\n"
      "  minivdb stats INDEX");
}

std::uint64_t integer(const char* text) {
  std::uint64_t value{};
  const std::string_view input(text);
  const auto [end, error] = std::from_chars(input.begin(), input.end(), value);
  if (error != std::errc{} || end != input.end()) throw std::invalid_argument("invalid integer");
  return value;
}

float number(const char* text) {
  std::size_t used{};
  const std::string input(text);
  const float value = std::stof(input, &used);
  if (used != input.size()) throw std::invalid_argument("invalid float");
  return value;
}

std::vector<float> vector_args(int argc, char** argv, int first) {
  std::vector<float> result;
  for (int i = first; i < argc; ++i) result.push_back(number(argv[i]));
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) usage();
    const std::string command(argv[1]);
    if (command == "build") {
      if (argc < 6 || argc > 8) usage();
      minivdb::Options options{static_cast<std::size_t>(integer(argv[3])),
                              minivdb::parse_metric(argv[4])};
      if (argc > 6) options.max_connections = static_cast<std::size_t>(integer(argv[6]));
      if (argc > 7) options.ef_construction = static_cast<std::size_t>(integer(argv[7]));
      minivdb::Index index(options);
      std::ifstream input(argv[5]);
      if (!input) throw std::runtime_error("cannot open input file");
      std::uint64_t id{};
      while (input >> id) {
        std::vector<float> vector(index.dimensions());
        for (float& value : vector)
          if (!(input >> value)) throw std::runtime_error("incomplete vector row");
        index.add(id, vector);
      }
      index.save(argv[2]);
      std::cout << "built " << index.size() << " vectors\n";
    } else if (command == "add") {
      if (argc < 5) usage();
      auto index = minivdb::Index::load(argv[2]);
      index.add(integer(argv[3]), vector_args(argc, argv, 4));
      index.save(argv[2]);
    } else if (command == "query") {
      if (argc < 6) usage();
      const auto index = minivdb::Index::load(argv[2]);
      const auto hits = index.search(vector_args(argc, argv, 5),
                                     static_cast<std::size_t>(integer(argv[3])),
                                     static_cast<std::size_t>(integer(argv[4])));
      std::cout << std::setprecision(7);
      for (const auto& hit : hits) std::cout << hit.id << '\t' << hit.distance << '\n';
    } else if (command == "stats") {
      if (argc != 3) usage();
      const auto index = minivdb::Index::load(argv[2]);
      std::cout << "vectors\t" << index.size() << "\ndimensions\t" << index.dimensions()
                << "\nmetric\t" << minivdb::metric_name(index.metric()) << '\n';
    } else {
      usage();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}

