#include "minivdb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>

namespace minivdb {
namespace {
constexpr std::array<char, 8> magic{'M', 'V', 'D', 'B', 'D', 'A', 'Y', '2'};

template <class T>
void write(std::ostream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(value));
  if (!out) throw std::runtime_error("failed to write index");
}

template <class T>
T read(std::istream& in) {
  T value{};
  in.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!in) throw std::runtime_error("truncated index file");
  return value;
}
}  // namespace

Metric parse_metric(std::string_view name) {
  if (name == "cosine") return Metric::cosine;
  if (name == "l2") return Metric::l2;
  throw std::invalid_argument("metric must be cosine or l2");
}

std::string_view metric_name(Metric metric) noexcept {
  return metric == Metric::cosine ? "cosine" : "l2";
}

Index::Index(Options options) : options_(options), random_state_(options.seed) {
  if (options.dimensions == 0) throw std::invalid_argument("dimensions must be positive");
  if (options.metric != Metric::cosine && options.metric != Metric::l2)
    throw std::invalid_argument("invalid metric");
  if (options.max_connections < 2) throw std::invalid_argument("M must be at least 2");
  if (options.ef_construction < options.max_connections)
    throw std::invalid_argument("ef_construction must be at least M");
}

std::vector<float> Index::prepare(std::span<const float> input) const {
  if (input.size() != options_.dimensions) throw std::invalid_argument("dimension mismatch");
  std::vector<float> vector(input.begin(), input.end());
  double norm = 0;
  for (float value : vector) {
    if (!std::isfinite(value)) throw std::invalid_argument("vectors must be finite");
    norm += static_cast<double>(value) * value;
  }
  if (options_.metric == Metric::cosine) {
    if (norm == 0) throw std::invalid_argument("cosine vectors must be non-zero");
    norm = std::sqrt(norm);
    for (float& value : vector) value = static_cast<float>(value / norm);
  }
  return vector;
}

float Index::distance(std::span<const float> a, std::span<const float> b) const noexcept {
  double result = 0;
  if (options_.metric == Metric::l2) {
    for (std::size_t i = 0; i < a.size(); ++i) {
      const double difference = static_cast<double>(a[i]) - b[i];
      result += difference * difference;
    }
    return static_cast<float>(result);
  }
  for (std::size_t i = 0; i < a.size(); ++i) result += static_cast<double>(a[i]) * b[i];
  return static_cast<float>(1.0 - std::clamp(result, -1.0, 1.0));
}

float Index::distance_to(std::span<const float> query, std::uint32_t slot) const noexcept {
  ++evaluations_;
  return distance(query, nodes_[slot].vector);
}

int Index::random_level() noexcept {
  std::uint64_t value = (random_state_ += 0x9e3779b97f4a7c15ULL);
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  const double uniform = static_cast<double>((value >> 11U) + 1U) / 9007199254740993.0;
  return std::min(32, static_cast<int>(-std::log(uniform) /
                                       std::log(static_cast<double>(options_.max_connections))));
}

std::uint32_t Index::greedy(std::span<const float> query, std::uint32_t entry,
                            int level) const {
  auto current = entry;
  float best = distance_to(query, current);
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto neighbor : nodes_[current].links[static_cast<std::size_t>(level)]) {
      const float candidate = distance_to(query, neighbor);
      if (Candidate{candidate, neighbor} < Candidate{best, current}) {
        current = neighbor;
        best = candidate;
        changed = true;
      }
    }
  }
  return current;
}

std::vector<Index::Candidate> Index::search_layer(std::span<const float> query,
                                                   std::uint32_t entry,
                                                   std::size_t ef, int level) const {
  std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> candidates;
  std::priority_queue<Candidate> results;
  std::vector<unsigned char> visited(nodes_.size());
  const Candidate first{distance_to(query, entry), entry};
  candidates.push(first);
  results.push(first);
  visited[entry] = 1;
  while (!candidates.empty()) {
    const auto current = candidates.top();
    candidates.pop();
    if (results.size() >= ef && current.first > results.top().first) break;
    for (auto neighbor : nodes_[current.second].links[static_cast<std::size_t>(level)]) {
      if (visited[neighbor]) continue;
      visited[neighbor] = 1;
      const Candidate next{distance_to(query, neighbor), neighbor};
      if (results.size() < ef || next < results.top()) {
        candidates.push(next);
        results.push(next);
        if (results.size() > ef) results.pop();
      }
    }
  }
  std::vector<Candidate> ordered;
  while (!results.empty()) {
    ordered.push_back(results.top());
    results.pop();
  }
  std::sort(ordered.begin(), ordered.end());
  return ordered;
}

void Index::prune(std::uint32_t slot, int level) {
  auto& links = nodes_[slot].links[static_cast<std::size_t>(level)];
  const std::size_t limit = level == 0 ? 2 * options_.max_connections : options_.max_connections;
  if (links.size() <= limit) return;
  std::sort(links.begin(), links.end(), [&](auto left, auto right) {
    return Candidate{distance(nodes_[slot].vector, nodes_[left].vector), left} <
           Candidate{distance(nodes_[slot].vector, nodes_[right].vector), right};
  });
  links.resize(limit);
}

void Index::add(std::uint64_t id, std::span<const float> input) {
  if (ids_.contains(id)) throw std::invalid_argument("duplicate id");
  if (nodes_.size() >= std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("too many vectors");
  const auto vector = prepare(input);
  const auto slot = static_cast<std::uint32_t>(nodes_.size());
  const int level = random_level();
  nodes_.push_back({id, vector, std::vector<std::vector<std::uint32_t>>(level + 1)});
  ids_.insert(id);
  if (entry_ < 0) {
    entry_ = slot;
    max_level_ = level;
    return;
  }
  auto entry = static_cast<std::uint32_t>(entry_);
  for (int layer = max_level_; layer > level; --layer) entry = greedy(vector, entry, layer);
  for (int layer = std::min(level, max_level_); layer >= 0; --layer) {
    auto neighbors = search_layer(vector, entry, options_.ef_construction, layer);
    const std::size_t limit = layer == 0 ? 2 * options_.max_connections : options_.max_connections;
    if (neighbors.size() > limit) neighbors.resize(limit);
    for (const auto& [unused, neighbor] : neighbors) {
      (void)unused;
      nodes_[slot].links[static_cast<std::size_t>(layer)].push_back(neighbor);
      nodes_[neighbor].links[static_cast<std::size_t>(layer)].push_back(slot);
      prune(neighbor, layer);
    }
    entry = neighbors.front().second;
  }
  if (level > max_level_) {
    entry_ = slot;
    max_level_ = level;
  }
}

std::vector<Hit> Index::search(std::span<const float> query, std::size_t k,
                               std::size_t ef_search) const {
  const auto prepared = prepare(query);
  evaluations_ = 0;
  if (nodes_.empty() || k == 0) return {};
  auto entry = static_cast<std::uint32_t>(entry_);
  for (int level = max_level_; level > 0; --level) entry = greedy(prepared, entry, level);
  const auto candidates = search_layer(prepared, entry, std::max(k, ef_search), 0);
  std::vector<Hit> hits;
  for (std::size_t i = 0; i < std::min(k, candidates.size()); ++i)
    hits.push_back({nodes_[candidates[i].second].id, candidates[i].first});
  return hits;
}

std::vector<Hit> Index::exact(std::span<const float> query, std::size_t k) const {
  const auto prepared = prepare(query);
  evaluations_ = 0;
  std::vector<Candidate> candidates;
  for (std::uint32_t slot = 0; slot < nodes_.size(); ++slot)
    candidates.emplace_back(distance_to(prepared, slot), slot);
  std::sort(candidates.begin(), candidates.end());
  std::vector<Hit> hits;
  for (std::size_t i = 0; i < std::min(k, candidates.size()); ++i)
    hits.push_back({nodes_[candidates[i].second].id, candidates[i].first});
  return hits;
}

void Index::save(const std::filesystem::path& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot open index file");
  out.write(magic.data(), magic.size());
  write(out, static_cast<std::uint32_t>(options_.dimensions));
  write(out, static_cast<std::uint8_t>(options_.metric));
  write(out, static_cast<std::uint32_t>(options_.max_connections));
  write(out, static_cast<std::uint32_t>(options_.ef_construction));
  write(out, options_.seed);
  write(out, random_state_);
  write(out, entry_);
  write(out, static_cast<std::int32_t>(max_level_));
  write(out, static_cast<std::uint32_t>(nodes_.size()));
  for (const auto& node : nodes_) {
    write(out, node.id);
    write(out, static_cast<std::uint8_t>(node.links.size()));
    out.write(reinterpret_cast<const char*>(node.vector.data()),
              static_cast<std::streamsize>(node.vector.size() * sizeof(float)));
    for (const auto& layer : node.links) {
      write(out, static_cast<std::uint32_t>(layer.size()));
      for (auto neighbor : layer) write(out, neighbor);
    }
  }
}

Index Index::load(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open index file");
  std::array<char, 8> header{};
  in.read(header.data(), header.size());
  if (header != magic) throw std::runtime_error("invalid index file");
  Options options;
  options.dimensions = read<std::uint32_t>(in);
  options.metric = static_cast<Metric>(read<std::uint8_t>(in));
  options.max_connections = read<std::uint32_t>(in);
  options.ef_construction = read<std::uint32_t>(in);
  options.seed = read<std::uint64_t>(in);
  Index index(options);
  index.random_state_ = read<std::uint64_t>(in);
  index.entry_ = read<std::int64_t>(in);
  index.max_level_ = read<std::int32_t>(in);
  const auto count = read<std::uint32_t>(in);
  for (std::uint32_t slot = 0; slot < count; ++slot) {
    Node node;
    node.id = read<std::uint64_t>(in);
    const auto levels = read<std::uint8_t>(in);
    if (levels == 0 || levels > 33 || !index.ids_.insert(node.id).second)
      throw std::runtime_error("invalid node data");
    node.vector.resize(options.dimensions);
    in.read(reinterpret_cast<char*>(node.vector.data()),
            static_cast<std::streamsize>(node.vector.size() * sizeof(float)));
    node.links.resize(levels);
    for (auto& layer : node.links) {
      const auto degree = read<std::uint32_t>(in);
      for (std::uint32_t edge = 0; edge < degree; ++edge) layer.push_back(read<std::uint32_t>(in));
    }
    index.nodes_.push_back(std::move(node));
  }
  if (count > 0 && (index.entry_ < 0 || static_cast<std::uint64_t>(index.entry_) >= count))
    throw std::runtime_error("invalid entry point");
  for (const auto& node : index.nodes_)
    for (std::size_t level = 0; level < node.links.size(); ++level)
      for (auto neighbor : node.links[level])
        if (neighbor >= count || index.nodes_[neighbor].links.size() <= level)
          throw std::runtime_error("invalid graph edge");
  return index;
}

}  // namespace minivdb
