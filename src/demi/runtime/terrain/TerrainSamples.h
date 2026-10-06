#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace demi::runtime {

// Immutable snapshots share sample pages. Editing copies only touched pages;
// the page size is storage granularity, not a limit on terrain dimensions.
// Reads never expose mutable references or detach storage. Use set() to edit.
template <class T> class TerrainSamples {
  static constexpr std::size_t PageSize = 1024;
  using Page = std::array<T, PageSize>;
  std::vector<std::shared_ptr<Page>> pages_;
  std::size_t size_ = 0;

  void writeSample(std::size_t index, T value) {
    auto &page = pages_[index / PageSize];
    if (page.use_count() != 1)
      page = std::make_shared<Page>(*page);
    (*page)[index % PageSize] = std::move(value);
  }

public:
  class Iterator {
    const TerrainSamples *owner_ = nullptr;
    std::size_t index_ = 0;

  public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = const T &;
    using pointer = const T *;
    using iterator_category = std::forward_iterator_tag;
    Iterator() = default;
    Iterator(const TerrainSamples *owner, std::size_t index)
        : owner_(owner), index_(index) {}
    reference operator*() const { return (*owner_)[index_]; }
    pointer operator->() const { return &**this; }
    Iterator &operator++() {
      ++index_;
      return *this;
    }
    Iterator operator++(int) {
      auto previous = *this;
      ++*this;
      return previous;
    }
    bool operator==(const Iterator &) const = default;
  };

  TerrainSamples() = default;
  TerrainSamples(const TerrainSamples &) = default;
  TerrainSamples &operator=(const TerrainSamples &) = default;
  TerrainSamples(TerrainSamples &&other) noexcept
      : pages_(std::move(other.pages_)), size_(std::exchange(other.size_, 0)) {
    other.pages_.clear();
  }
  TerrainSamples &operator=(TerrainSamples &&other) noexcept {
    if (this != &other) {
      pages_ = std::move(other.pages_);
      size_ = std::exchange(other.size_, 0);
      other.pages_.clear();
    }
    return *this;
  }
  TerrainSamples(std::initializer_list<T> values) {
    assign(values.begin(), values.end());
  }
  TerrainSamples(const std::vector<T> &values) {
    assign(values.begin(), values.end());
  }
  TerrainSamples &operator=(std::initializer_list<T> values) {
    assign(values.begin(), values.end());
    return *this;
  }
  TerrainSamples &operator=(const std::vector<T> &values) {
    assign(values.begin(), values.end());
    return *this;
  }
  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  std::size_t pageCount() const { return pages_.size(); }
  const void *pageIdentity(std::size_t sample) const {
    return pages_.at(sample / PageSize).get();
  }

  const T &operator[](std::size_t index) const {
    return (*pages_[index / PageSize])[index % PageSize];
  }
  const T &at(std::size_t index) const {
    if (index >= size_)
      throw std::out_of_range("Terrain sample outside buffer");
    return (*this)[index];
  }
  void set(std::size_t index, T value) {
    if (index >= size_)
      throw std::out_of_range("Terrain sample outside buffer");
    writeSample(index, std::move(value));
  }
  const T &front() const { return at(0); }
  const T &back() const { return at(size_ - 1); }
  void resize(std::size_t count) {
    const auto pageCount = count / PageSize + (count % PageSize != 0);
    const auto oldPageCount = pages_.size();
    pages_.resize(pageCount);
    try {
      for (std::size_t page = oldPageCount; page < pageCount; ++page)
        pages_[page] = std::make_shared<Page>();
      // Values in the previous partial page may survive shrink/regrow.
      if (count > size_ && oldPageCount != 0) {
        const auto end = std::min(count, oldPageCount * PageSize);
        for (auto index = size_; index < end; ++index)
          writeSample(index, T{});
      }
    } catch (...) {
      // Failed growth must not leave null pages in a reusable buffer.
      pages_.resize(oldPageCount);
      throw;
    }
    size_ = count;
  }
  void clear() {
    pages_.clear();
    size_ = 0;
  }
  void push_back(T value) {
    const auto index = size_;
    resize(size_ + 1);
    set(index, std::move(value));
  }
  void pop_back() {
    if (!empty())
      resize(size_ - 1);
  }
  template <std::input_iterator Input> void assign(Input first, Input last) {
    TerrainSamples replacement;
    for (; first != last; ++first)
      replacement.push_back(*first);
    *this = std::move(replacement);
  }
  void assign(std::size_t count, T value) {
    resize(count);
    for (std::size_t index = 0; index < count; ++index)
      set(index, value);
  }
  Iterator begin() const { return {this, 0}; }
  Iterator end() const { return {this, size_}; }
  bool operator==(const TerrainSamples &other) const {
    if (size_ != other.size_)
      return false;
    for (std::size_t page = 0; page < pages_.size(); ++page) {
      if (pages_[page] == other.pages_[page])
        continue;
      const auto count = std::min(PageSize, size_ - page * PageSize);
      if (!std::equal(pages_[page]->begin(), pages_[page]->begin() + count,
                      other.pages_[page]->begin()))
        return false;
    }
    return true;
  }
  bool operator==(const std::vector<T> &other) const {
    return size_ == other.size() && std::equal(begin(), end(), other.begin());
  }
};
} // namespace demi::runtime
