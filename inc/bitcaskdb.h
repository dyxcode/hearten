#ifndef HEARTEN_BITCASKDB_H_
#define HEARTEN_BITCASKDB_H_

#include <string>
#include <unordered_map>
#include <optional>
#include <filesystem>

#include "log.h"

namespace hearten {

namespace detail {

namespace fs = std::filesystem;

inline fs::path dataPath(uint32_t id, const fs::path& dir)
{ return dir / ("storage" + std::to_string(id)); }

inline fs::path tempPath(uint32_t id, const fs::path& dir)
{ return dir / ("temp" + std::to_string(id)); }

inline fs::path activePath(const fs::path& dir)
{ return dir / "active"; }

inline fs::path hintPath(const fs::path& dir)
{ return dir / "hint"; }

class Record : Noncopyable {
public:
  Record(std::string key, std::string value)
    : key_(std::move(key)), value_(std::move(value)) { }

  explicit Record(std::istream& os) {
    uint32_t size;
    os.read(reinterpret_cast<char*>(&size), sizeof(size));
    key_.resize(size);
    os.read(reinterpret_cast<char*>(&size), sizeof(size));
    value_.resize(size);
    os.read(key_.data(), key_.size());
    os.read(value_.data(), value_.size());
  }

  Record(Record&& record) = default;
  Record& operator=(Record&& record) = default;

  uint32_t putInStream(std::ostream& file) const {
    auto offset = file.tellp();
    ASSERT(offset != -1);
    uint32_t size = key_.size();
    file.write(reinterpret_cast<char*>(&size), sizeof(size));
    size = value_.size();
    file.write(reinterpret_cast<char*>(&size), sizeof(size));
    file.write(key_.data(), key_.size());
    file.write(value_.data(), value_.size());
    return static_cast<uint32_t>(offset);
  }

  std::string key() const { return key_; }
  std::string value() const { return value_; }

private:
  std::string key_;
  std::string value_;
};


class Position {
  static constexpr uint32_t kMaxUint32 = std::numeric_limits<uint32_t>::max();
public:
  Position(uint32_t fileid, uint32_t offset)
    : fileid_(fileid), offset_(offset) { }

  explicit Position(std::istream& os) {
    os.read(reinterpret_cast<char*>(&fileid_), sizeof(fileid_));
    os.read(reinterpret_cast<char*>(&offset_), sizeof(offset_));
  }

  detail::Record getRecord(const fs::path& dir) const {
    std::ifstream fs{fileid_ == kMaxUint32 ?
      activePath(dir) : dataPath(fileid_, dir), std::ios::binary};
    ASSERT(fs.is_open());
    return getRecord(fs);
  }
  detail::Record getRecord(std::istream& os) const {
    os.seekg(static_cast<std::ofstream::pos_type>(offset_));
    return Record(os);
  }

  void putInStream(std::ostream& os) const {
    os.write(reinterpret_cast<const char*>(&fileid_), sizeof(fileid_));
    os.write(reinterpret_cast<const char*>(&offset_), sizeof(offset_));
  }

  uint32_t getFileId() const { return fileid_; }

private:
  uint32_t fileid_;
  uint32_t offset_;
};

} // namespace detail

class BitcaskDB {
  static constexpr uint32_t kMaxUint32 = std::numeric_limits<uint32_t>::max();
public:
  BitcaskDB(const std::string& name, const std::string& dir_path)
  : dir_(dir_path + name + ".database") {
    detail::fs::create_directory(dir_);

    std::ifstream hint_file{detail::hintPath(dir_), std::ios::binary};
    if (hint_file.is_open()) buildIndexesFromHint(hint_file);

    std::ifstream active_file{detail::activePath(dir_), std::ios::binary};
    if (active_file.is_open()) buildIndexesFromActive(active_file);

    active_.open(detail::activePath(dir_), std::ios::app | std::ios::binary);
  }

  BitcaskDB& put(std::string key, std::string value) {
    detail::Record record{std::move(key), std::move(value)};
    uint32_t offset = record.putInStream(active_);
    detail::Position position{kMaxUint32, offset};
    indexes_.insert_or_assign(record.key(), position);
    if (active_.tellp() >= kMaxUint32) {
      rebuildData();
      rebuildHint();
      active_.close();
      active_.clear();
      detail::fs::resize_file(detail::activePath(dir_), 0);
      active_.open(detail::activePath(dir_), std::ios::app | std::ios::binary);
      ASSERT(active_.is_open());
    }
    return *this;
  }

  std::optional<std::string> get(const std::string& key) {
    auto ret = indexes_.find(key);
    if (ret == indexes_.end()) return std::nullopt;
    const auto& position = ret->second;
    auto record = position.getRecord(dir_);
    ASSERT(record.key() == key);
    return record.value();
  }

  BitcaskDB& remove(const std::string& key) {
    auto ret = indexes_.erase(key);
    if (ret == 1) {
      detail::Record("", "").putInStream(active_);
    }
    return *this;
  }

private:
  void buildIndexesFromHint(std::ifstream& hint_file) {
    while (hint_file.peek() != std::ifstream::traits_type::eof()) {
      uint32_t key_size;
      std::string key;
      hint_file.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
      key.resize(key_size);
      hint_file.read(key.data(), key_size);
      detail::Position position{hint_file};
      indexes_.emplace(std::move(key), position);
    }
  }
  void buildIndexesFromActive(std::ifstream& active_file) {
    while (active_file.peek() != std::ifstream::traits_type::eof()) {
      auto offset = static_cast<uint32_t>(active_file.tellg());
      detail::Record record{active_file};
      if (record.key().empty() && record.value().empty()) continue;
      detail::Position position{kMaxUint32, offset};
      indexes_.insert_or_assign(record.key(), position);
    }
  }
  void rebuildData() {
    DEBUG << "rebuildData begin";
    std::ifstream active_fs{detail::activePath(dir_), std::ios::binary};
    std::vector<std::ifstream> fs_vec;
    for (uint32_t id = 0; ; ++id) {
      detail::fs::path path{detail::dataPath(id, dir_)};
      if (!detail::fs::exists(path)) break;
      detail::fs::path temp_path{detail::tempPath(id, dir_)};
      detail::fs::rename(path, temp_path);
      std::ifstream fs{temp_path, std::ios::binary};
      ASSERT(fs.is_open());
      fs_vec.push_back(std::move(fs));
    }

    std::unordered_map<std::string, detail::Position> new_indexes;
    uint32_t new_id = 0;
    std::ofstream fs;

    for (auto&& [key, position] : indexes_) {
      if (!fs.is_open()) {
        fs.open(detail::dataPath(new_id++, dir_),
            std::ios::app | std::ios::binary);
        ASSERT(fs.is_open());
      }
      auto record = position.getFileId() == kMaxUint32 ?
                    position.getRecord(active_fs) :
                    position.getRecord(fs_vec[position.getFileId()]);
      ASSERT(record.key() == key);
      uint32_t offset = record.putInStream(fs);
      detail::Position new_position{new_id - 1, offset};
      new_indexes.insert_or_assign(record.key(), new_position);
      DEBUG << "transfer one";
      if (fs.tellp() >= kMaxUint32) {
        fs.close();
        fs.clear();
      }
    }
    fs.close();
    indexes_.swap(new_indexes);

    for (uint32_t id = 0; id != fs_vec.size(); ++id) {
      fs_vec[id].close();
      detail::fs::remove(detail::tempPath(id, dir_));
    }
    DEBUG << "rebuildData end";
  }
  void rebuildHint() {
    DEBUG << "rebuildHint begin";

    std::ofstream hint_file{detail::hintPath(dir_),
      std::ios::binary | std::ios::trunc};
    for (auto&& [key, position] : indexes_) {
      uint32_t key_size = key.size();
      hint_file.write(reinterpret_cast<char*>(&key_size), sizeof(key_size));
      hint_file.write(key.data(), key.size());
      position.putInStream(hint_file);
    }
    DEBUG << "rebuildHint end";
  }

  detail::fs::path dir_;
  std::ofstream active_;
  std::unordered_map<std::string, detail::Position> indexes_;
};

} // namespace hearten

#endif
