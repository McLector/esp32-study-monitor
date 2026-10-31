// Host-side stand-in for the ESP32 core's Preferences (NVS): an in-memory
// key/value store that tests can inspect, pre-load with bad data, or make fail.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fake {
inline std::map<std::string, std::vector<uint8_t>> nvs;  // "namespace/key" -> bytes
inline int nvsWrites = 0;
inline bool nvsFailWrites = false;  // simulate a full or worn flash
inline void nvsReset() { nvs.clear(); nvsWrites = 0; nvsFailWrites = false; }
inline void nvsPut(const char* ns, const char* key, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  nvs[std::string(ns) + "/" + key] = std::vector<uint8_t>(b, b + n);
}
}  // namespace fake

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false) {
    ns_ = name;
    if (!readOnly) return true;
    // Like the real thing, a read-only open fails until the namespace exists.
    for (const auto& kv : fake::nvs)
      if (kv.first.compare(0, ns_.size() + 1, ns_ + "/") == 0) return true;
    ns_.clear();
    return false;
  }
  void end() { ns_.clear(); }

  size_t putBytes(const char* key, const void* value, size_t len) {
    if (ns_.empty() || fake::nvsFailWrites) return 0;
    fake::nvsPut(ns_.c_str(), key, value, len);
    fake::nvsWrites++;
    return len;
  }

  // Real behaviour: 0 if the key is missing or the stored blob is larger than
  // maxLen; otherwise copies the blob (possibly shorter) and returns its size.
  size_t getBytes(const char* key, void* buf, size_t maxLen) {
    if (ns_.empty()) return 0;
    auto it = fake::nvs.find(ns_ + "/" + key);
    if (it == fake::nvs.end() || it->second.size() > maxLen) return 0;
    memcpy(buf, it->second.data(), it->second.size());
    return it->second.size();
  }

 private:
  std::string ns_;
};
