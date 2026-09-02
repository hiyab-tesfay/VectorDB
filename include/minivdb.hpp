#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace minivdb {

enum class Metric : std::uint8_t { cosine, l2 };
Metric parse_metric(std::string_view name);
std::string_view metric_name(Metric metric) noexcept;

struct Options {
  std::size_t dimensions{};
  Metric metric{Metric::cosine};
  std::size_t max_connections{12};
  std::size_t ef_construction{80};
  std::uint64_t seed{42};
};

struct Hit {
  std::uint64_t id{};
  float distance{};
};

class Index {
 public:
  explicit Index(Options options);
  void add(std::uint64_t id, std::span<const float> vector);
  std::vector<Hit> search(std::span<const float> query, std::size_t k,
                          std::size_t ef_search = 50) const;
  std::vector<Hit> exact(std::span<const float> query, std::size_t k) const;
  void save(const std::filesystem::path& path) const;
  static Index load(const std::filesystem::path& path);

  std::size_t size() const noexcept { return nodes_.size(); }
  std::size_t dimensions() const noexcept { return options_.dimensions; }
  Metric metric() const noexcept { return options_.metric; }
  std::size_t distance_evaluations() const noexcept { return evaluations_; }

 private:
  struct Node {
    std::uint64_t id{};
    std::vector<float> vector;
    std::vector<std::vector<std::uint32_t>> links;
  };
  using Candidate = std::pair<float, std::uint32_t>;
  Options options_;
  std::vector<Node> nodes_;
  std::unordered_set<std::uint64_t> ids_;
  std::int64_t entry_{-1};
  int max_level_{-1};
  std::uint64_t random_state_{};
  mutable std::size_t evaluations_{};

  std::vector<float> prepare(std::span<const float> vector) const;
  float distance(std::span<const float> a, std::span<const float> b) const noexcept;
  float distance_to(std::span<const float> query, std::uint32_t slot) const noexcept;
  int random_level() noexcept;
  std::uint32_t greedy(std::span<const float> query, std::uint32_t entry, int level) const;
  std::vector<Candidate> search_layer(std::span<const float> query, std::uint32_t entry,
                                      std::size_t ef, int level) const;
  void prune(std::uint32_t slot, int level);
};

}  // namespace minivdb

