
#ifndef PROPERTIES_H
#define PROPERTIES_H

#include <map>
#include <string>

namespace kvcache {

class Properties {
 public:
  std::string GetProperty(
      const std::string& key,
      const std::string& default_value = std::string()) const {
    auto it = properties_.find(key);
    if (it != properties_.end()) {
      return it->second;
    }
    return default_value;
  }

  void SetProperty(const std::string& key, const std::string& value) {
    properties_[key] = value;
  }

  const std::string& operator[](const std::string& key) const {
    return properties_.at(key);
  }

 private:
  std::map<std::string, std::string> properties_;
};

}  // namespace kvcache

#endif