#include "minivdb.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <vector>

int main() {
  constexpr std::size_t count = 5'000, dimensions = 32, query_count = 100, k = 10;
  std::mt19937 random(42);
  std::normal_distribution<float> normal;
  const auto make_vector = [&] {
    std::vector<float> vector(dimensions);
    for (float& value : vector) value = normal(random);
    return vector;
  };
  minivdb::Index index({dimensions, minivdb::Metric::cosine, 16, 100, 42});
  const auto build_start = std::chrono::steady_clock::now();
  for (std::size_t id = 0; id < count; ++id) index.add(id, make_vector());
  const auto build_end = std::chrono::steady_clock::now();
  std::vector<std::vector<float>> queries;
  for (std::size_t i = 0; i < query_count; ++i) queries.push_back(make_vector());
  std::vector<std::vector<minivdb::Hit>> truth;
  const auto exact_start = std::chrono::steady_clock::now();
  for (const auto& query : queries) truth.push_back(index.exact(query, k));
  const auto exact_end = std::chrono::steady_clock::now();
  double matches = 0;
  const auto ann_start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < queries.size(); ++i) {
    std::set<std::uint64_t> ids;
    for (const auto& hit : truth[i]) ids.insert(hit.id);
    for (const auto& hit : index.search(queries[i], k, 64)) matches += ids.contains(hit.id);
  }
  const auto ann_end = std::chrono::steady_clock::now();
  const auto seconds = [](auto start, auto end) {
    return std::chrono::duration<double>(end - start).count();
  };
  const double build = seconds(build_start, build_end);
  const double exact = seconds(exact_start, exact_end);
  const double ann = seconds(ann_start, ann_end);
  std::cout << std::fixed << std::setprecision(3) << "vectors=" << count
            << " dimensions=" << dimensions << "\nbuild_seconds=" << build
            << "\nexact_qps=" << query_count / exact << "\nhnsw_qps=" << query_count / ann
            << "\nrecall@10=" << matches / static_cast<double>(query_count * k) << '\n';
}

