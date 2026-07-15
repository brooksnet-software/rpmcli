// Json.hpp - a minimal JSON value used throughout the RPM client.
//
// The rest of the code includes only this header and never touches a JSON
// library directly. The backend is selected at compile time:
//
//   (default)          -> nlohmann/json   (vendored single header)
//   -DRPM_JSON_BOOST   -> boost::json      (header-only via boost/json/src.hpp)
//
// The RPM RPC protocol only uses flat JSON objects whose values are strings,
// integers, booleans or arrays, so the surface exposed here is deliberately
// small.
#ifndef RPM_JSON_HPP
#define RPM_JSON_HPP

#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

#if defined(RPM_JSON_BOOST)
  #include <boost/json.hpp>
#else
  #include <nlohmann/json.hpp>
#endif

namespace rpm {

// Thrown when a document fails to parse.
struct JsonError : std::runtime_error {
  explicit JsonError(const std::string& what) : std::runtime_error(what) {}
};

class Json {
public:
#if defined(RPM_JSON_BOOST)
  using Native = boost::json::value;
#else
  using Native = nlohmann::json;
#endif

  // Construct a null value.
  Json() = default;

  // Scalar constructors (non-explicit for ergonomic object building).
  Json(const char* s);
  Json(const std::string& s);
  Json(bool b);
  Json(int n);
  Json(long long n);

  // Wrap an existing backend value.
  explicit Json(Native v) : v_(std::move(v)) {}

  // Factories.
  static Json object();
  static Json parse(const std::string& text);   // throws JsonError

  // Serialization.
  std::string dump() const;

  // Object building. Ensures this value is an object and sets key -> value.
  void set(const std::string& key, const Json& value);

  // Type queries.
  bool isNull()   const;
  bool isObject() const;
  bool isArray()  const;
  bool isString() const;
  bool isBool()   const;

  bool contains(const std::string& key) const;   // object membership

  // Access. at() returns a null Json if the key is absent.
  Json at(const std::string& key) const;

  // Typed extraction with defaults (never throw).
  std::string asString(const std::string& def = "") const;
  bool        asBool(bool def = false) const;
  long long   asInt(long long def = 0) const;

  // Array elements (empty if not an array).
  std::vector<Json> items() const;

  // Object members as (key, value) pairs (empty if not an object).
  std::vector<std::pair<std::string, Json>> objectItems() const;

  const Native& native() const { return v_; }

private:
  Native v_{};
};

// ----------------------------------------------------------------------------
// Inline implementation - one small body per backend.
// ----------------------------------------------------------------------------
#if defined(RPM_JSON_BOOST)

inline Json::Json(const char* s)        : v_(boost::json::string(s)) {}
inline Json::Json(const std::string& s) : v_(boost::json::string(s)) {}
inline Json::Json(bool b)               : v_(b) {}
inline Json::Json(int n)                : v_(static_cast<std::int64_t>(n)) {}
inline Json::Json(long long n)          : v_(static_cast<std::int64_t>(n)) {}

inline Json Json::object() { return Json(Native(boost::json::object())); }

inline Json Json::parse(const std::string& text) {
  try {
    return Json(boost::json::parse(text));
  } catch (const std::exception& e) {
    throw JsonError(e.what());
  }
}

inline std::string Json::dump() const { return boost::json::serialize(v_); }

inline void Json::set(const std::string& key, const Json& value) {
  if (!v_.is_object()) v_ = boost::json::object();
  v_.as_object()[key] = value.v_;
}

inline bool Json::isNull()   const { return v_.is_null(); }
inline bool Json::isObject() const { return v_.is_object(); }
inline bool Json::isArray()  const { return v_.is_array(); }
inline bool Json::isString() const { return v_.is_string(); }
inline bool Json::isBool()   const { return v_.is_bool(); }

inline bool Json::contains(const std::string& key) const {
  return v_.is_object() && v_.as_object().contains(key);
}

inline Json Json::at(const std::string& key) const {
  if (!contains(key)) return Json();
  return Json(v_.as_object().at(key));
}

inline std::string Json::asString(const std::string& def) const {
  if (!v_.is_string()) return def;
  const auto& s = v_.as_string();
  return std::string(s.c_str(), s.size());
}

inline bool Json::asBool(bool def) const {
  return v_.is_bool() ? v_.as_bool() : def;
}

inline long long Json::asInt(long long def) const {
  if (v_.is_int64())  return v_.as_int64();
  if (v_.is_uint64()) return static_cast<long long>(v_.as_uint64());
  if (v_.is_double()) return static_cast<long long>(v_.as_double());
  return def;
}

inline std::vector<Json> Json::items() const {
  std::vector<Json> out;
  if (v_.is_array())
    for (const auto& e : v_.as_array()) out.push_back(Json(e));
  return out;
}

inline std::vector<std::pair<std::string, Json>> Json::objectItems() const {
  std::vector<std::pair<std::string, Json>> out;
  if (v_.is_object())
    for (const auto& kv : v_.as_object())
      out.emplace_back(std::string(kv.key()), Json(kv.value()));
  return out;
}

#else  // nlohmann/json

inline Json::Json(const char* s)        : v_(s) {}
inline Json::Json(const std::string& s) : v_(s) {}
inline Json::Json(bool b)               : v_(b) {}
inline Json::Json(int n)                : v_(n) {}
inline Json::Json(long long n)          : v_(n) {}

inline Json Json::object() { return Json(Native::object()); }

inline Json Json::parse(const std::string& text) {
  try {
    return Json(Native::parse(text));
  } catch (const std::exception& e) {
    throw JsonError(e.what());
  }
}

inline std::string Json::dump() const { return v_.dump(); }

inline void Json::set(const std::string& key, const Json& value) {
  if (!v_.is_object()) v_ = Native::object();
  v_[key] = value.v_;
}

inline bool Json::isNull()   const { return v_.is_null(); }
inline bool Json::isObject() const { return v_.is_object(); }
inline bool Json::isArray()  const { return v_.is_array(); }
inline bool Json::isString() const { return v_.is_string(); }
inline bool Json::isBool()   const { return v_.is_boolean(); }

inline bool Json::contains(const std::string& key) const {
  return v_.is_object() && v_.contains(key);
}

inline Json Json::at(const std::string& key) const {
  if (!contains(key)) return Json();
  return Json(v_.at(key));
}

inline std::string Json::asString(const std::string& def) const {
  return v_.is_string() ? v_.get<std::string>() : def;
}

inline bool Json::asBool(bool def) const {
  return v_.is_boolean() ? v_.get<bool>() : def;
}

inline long long Json::asInt(long long def) const {
  return v_.is_number() ? v_.get<long long>() : def;
}

inline std::vector<Json> Json::items() const {
  std::vector<Json> out;
  if (v_.is_array())
    for (const auto& e : v_) out.push_back(Json(e));
  return out;
}

inline std::vector<std::pair<std::string, Json>> Json::objectItems() const {
  std::vector<std::pair<std::string, Json>> out;
  if (v_.is_object())
    for (auto it = v_.begin(); it != v_.end(); ++it)
      out.emplace_back(it.key(), Json(it.value()));
  return out;
}

#endif

}  // namespace rpm

#endif  // RPM_JSON_HPP
