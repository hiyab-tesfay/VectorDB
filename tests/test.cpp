#include "minivdb.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <vector>

int main() {
  int failures = 0;
  const auto check = [&](bool condition, const char* message) {
    if (!condition) {
      ++failures;
      std::cerr << "FAIL: " << message << '\n';
    }
  };
  const auto throws = [&](auto function) {
    try {
      function();
      return false;
    } catch (const std::exception&) {
      return true;
    }
  };

  minivdb::Index cosine({2, minivdb::Metric::cosine, 4, 20, 42});
  cosine.add(1, std::vector<float>{1, 0});
  cosine.add(2, std::vector<float>{0.8F, 0.2F});
  cosine.add(3, std::vector<float>{0, 1});
  const auto hits = cosine.search(std::vector<float>{1, 0}, 3, 20);
  check(hits.size() == 3 && hits[0].id == 1 && hits[1].id == 2, "cosine ranking");
  check(throws([&] { cosine.add(1, std::vector<float>{1, 0}); }), "duplicate id");
  check(throws([&] { cosine.add(4, std::vector<float>{1}); }), "dimension check");
  check(throws([&] { cosine.add(4, std::vector<float>{0, 0}); }), "zero vector check");

  minivdb::Index l2({2, minivdb::Metric::l2, 4, 20, 42});
  l2.add(1, std::vector<float>{1, 0});
  l2.add(2, std::vector<float>{3, 0});
  check(std::abs(l2.search(std::vector<float>{0, 0}, 1)[0].distance - 1) < 1e-6F,
        "L2 distance");

  const auto path = std::filesystem::temp_directory_path() / "minivdb-test.bin";
  cosine.save(path);
  const auto loaded = minivdb::Index::load(path);
  check(loaded.search(std::vector<float>{1, 0}, 3)[0].id == 1, "save/load");
  std::filesystem::remove(path);

  constexpr std::size_t count = 600, dimensions = 16, queries = 30, k = 10;
  std::mt19937 random(7);
  std::normal_distribution<float> normal;
  minivdb::Index ann({dimensions, minivdb::Metric::cosine, 12, 80, 9});
  for (std::size_t id = 0; id < count; ++id) {
    std::vector<float> vector(dimensions);
    for (float& value : vector) value = normal(random);
    ann.add(id, vector);
  }
  double matches = 0;
  for (std::size_t q = 0; q < queries; ++q) {
    std::vector<float> query(dimensions);
    for (float& value : query) value = normal(random);
    std::set<std::uint64_t> exact;
    for (const auto& hit : ann.exact(query, k)) exact.insert(hit.id);
    for (const auto& hit : ann.search(query, k, 64)) matches += exact.contains(hit.id);
  }
  const double recall = matches / static_cast<double>(queries * k);
  std::cout << "recall@10=" << recall << '\n';
  check(recall >= 0.85, "ANN recall");
  if (failures == 0) std::cout << "all tests passed\n";
  return failures == 0 ? 0 : 1;
}

