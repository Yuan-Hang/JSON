#include "json.h"
#include <cassert>
#include <cmath>      //HUGE_VAL
#include <cstdlib>    //strtod
#include <cstring>    //strncmp
#include <stdexcept>  //runtime_error
#include <utility>    //move
using namespace std;
using namespace json;
using namespace details;

namespace details {
class JsonValue {
 public:
  virtual Json::JsonType type() const = 0;

  virtual bool asBool() const { throw runtime_error("not a boolean"); }
  virtual double asDouble() const { throw runtime_error("not a number"); }
  virtual const string& asString() const {
    throw runtime_error("not a string");
  }
  virtual Json::array_t& asArray() { throw runtime_error("not an array"); }
  virtual const Json::array_t& asArray() const {
    throw runtime_error("not an array");
  }
  virtual Json::object_t& asObect() { throw runtime_error("not an object"); }
  virtual const Json::object_t& asObect() const {
    throw runtime_error("not an object");
  }
  virtual Json& operator[](size_t) { throw runtime_error("not an array"); }
  virtual const Json& operator[](size_t) const {
    throw runtime_error("not an array");
  }
  virtual Json& operator[](const string&) {
    throw runtime_error("not an object");
  }
  virtual const Json& operator[](const string&) const {
    throw runtime_error("not an object");
  }

  virtual size_t size() const { return 0; }

  virtual ~JsonValue() {}

  JsonValue() {}
};

class JsonNull final : public JsonValue {
 public:
  explicit JsonNull() {}
  Json::JsonType type() const override { return Json::JsonType::kNull; }
};

class JsonBool final : public JsonValue {
 public:
  explicit JsonBool(bool val) : val_(val) {}
  Json::JsonType type() const override { return Json::JsonType::kBool; }
  bool asBool() const override { return val_; }

 private:
  bool val_;
};

class JsonNumber final : public JsonValue {
 public:
  explicit JsonNumber(double val) : val_(val) {}
  Json::JsonType type() const override { return Json::JsonType::kNumber; }
  double asDouble() const override { return val_; }

 private:
  double val_;
};

class JsonString final : public JsonValue {
 public:
  explicit JsonString(string val) : val_(val) {}
  Json::JsonType type() const override { return Json::JsonType::kString; }
  const string& asString() const override { return val_; }

 private:
  string val_;
};

class JsonArray final : public JsonValue {
 public:
  explicit JsonArray(const Json::array_t& val) : val_(val) {}
  Json::JsonType type() const override { return Json::JsonType::kArray; }
  Json::array_t& asArray() override { return val_; }
  const Json::array_t& asArray() const override { return val_; }
  const Json& operator[](size_t i) const override { return val_[i]; }
  Json& operator[](size_t i) override { return val_[i]; }
  size_t size() const override { return val_.size(); }

 private:
  Json::array_t val_;
};

class JsonObject final : public JsonValue {
 public:
  explicit JsonObject(const Json::object_t& val) : val_(val) {}
  Json::JsonType type() const override { return Json::JsonType::kObject; }
  Json::object_t& asObect() override { return val_; }
  const Json::object_t& asObect() const override { return val_; }
  const Json& operator[](const string& i) const override { return val_.at(i); }
  Json& operator[](const string& i) override { return val_.at(i); }
  size_t size() const override { return val_.size(); }

 private:
  Json::object_t val_;
};
}  // namespace details

namespace details {
bool is1to9(char ch) { return ch >= '1' && ch <= '9'; }
bool is0to9(char ch) { return ch >= '0' && ch <= '9'; }

class Parser final {
 public:
  Parser(const string& content)
      : start_(content.c_str()), curr_(content.c_str()) {}

  Json parse() {
    parseWhitespace();
    Json json = parseValue();
    parseWhitespace();
    if (*curr_) error("ROOT NOT SINGULAR");
    return json;
  }

 private:
  Json parseValue() {
    switch (*start_) {
      case 'n':
        return parseLiteral("null");
      case 't':
        return parseLiteral("true");
      case 'f':
        return parseLiteral("false");
      case '\"':
        return parseString();
      case '[':
        return parseArray();
      case '{':
        return parseObject();
      case '\0':
        error("EXPECT VALUE");
      default:
        return parseNumber();
    }
  }

  Json parseLiteral(const string& literal) {
    if (strncmp(curr_, literal.c_str(), literal.size())) error("INVALID VALUE");
    curr_ += literal.size();
    start_ = curr_;
    switch (literal[0]) {
      case 't':
        return Json(true);
      case 'f':
        return Json(false);
      default:
        return Json(nullptr);
    }
  }

  Json parseNumber() {
    if (*curr_ == '-') ++curr_;
    // integer
    if (*curr_ == '0')
      ++curr_;
    else {
      if (!is1to9(*curr_)) error("INVALID VALUE");
      while (is0to9(*++curr_))
        ;
    }
    // frac
    if (*curr_ == '.') {
      if (!is0to9(*++curr_)) error("INVALID VALUE");
      while (is0to9(*++curr_))
        ;
    }
    // exp
    if (toupper(*curr_) == 'E') {
      ++curr_;
      if (*curr_ == '-' || *curr_ == '+') ++curr_;
      if (!is0to9(*curr_)) error("INVALID VALUE");
      while (is0to9(*++curr_))
        ;
    }
    double val = strtod(start_, nullptr);
    if (fabs(val) == HUGE_VAL) error("NUMBER TOO BIG");
    start_ = curr_;
    return Json(val);
  }

  Json parseString() { return Json(parseRawString()); }

  string parseRawString() {
    string str;
    while (1) {
      switch (*++curr_) {
        case '\"':
          start_ = ++curr_;
          return str;
        case '\0':
          error("MISS QUOTATION MARK");
        case '\\':
          switch (*++curr_) {
            case '\"':
              str.push_back('\"');
              break;
            case '\\':
              str.push_back('\\');
              break;
            case '/':
              str.push_back('/');
              break;
            case 'b':
              str.push_back('\b');
              break;
            case 'f':
              str.push_back('\f');
              break;
            case 'n':
              str.push_back('\n');
              break;
            case 't':
              str.push_back('\t');
              break;
            case 'r':
              str.push_back('\r');
              break;
            case 'u': {
              unsigned u1 = parse4hex();
              if (u1 >= 0xd800 && u1 <= 0xdbff) {  // high surrogate
                if (*++curr_ != '\\') error("INVALID UNICODE SURROGATE");
                if (*++curr_ != 'u') error("INVALID UNICODE SURROGATE");
                unsigned u2 = parse4hex();  // low surrogate
                if (u2 < 0xdc00 || u2 > 0xdfff)
                  error("INVALID UNICODE SURROGATE");
                u1 = (((u1 - 0xd800) << 10) | (u2 - 0xdc00)) + 0x10000;
              }
              str += encodeUTF8(u1);
            } break;
            case '\0':
              error("MISS QUOTATION MARK");
            default:
              error("INVALID STRING ESCAPE");
          }
          break;
        default:
          if (static_cast<unsigned char>(*curr_) < 0x20)
            error("INVALID STRING CHAR");
          str.push_back(*curr_);
      }
    }
  }

  unsigned parse4hex() {
    unsigned u = 0;
    for (int i = 0; i != 4; ++i) {
      // now *curr_ = "uXXXX...."
      unsigned ch = static_cast<unsigned>(toupper(*++curr_));
      u <<= 4;  // u *= 16
      if (ch >= '0' && ch <= '9')
        u |= (ch - '0');
      else if (ch >= 'A' && ch <= 'F')
        u |= ch - 'A' + 10;
      else
        error("INVALID UNICODE HEX");
    }
    return u;
  }

  string encodeUTF8(unsigned u) {
    string utf8;
    if (u <= 0x7F)  // 0111,1111
      utf8.push_back(static_cast<char>(u & 0xff));
    else if (u <= 0x7FF) {
      utf8.push_back(static_cast<char>(0xc0 | ((u >> 6) & 0xff)));
      utf8.push_back(static_cast<char>(0x80 | (u & 0x3f)));
    } else if (u <= 0xFFFF) {
      utf8.push_back(static_cast<char>(0xe0 | ((u >> 12) & 0xff)));
      utf8.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3f)));
      utf8.push_back(static_cast<char>(0x80 | (u & 0x3f)));
    } else {
      assert(u <= 0x10FFFF);
      utf8.push_back(static_cast<char>(0xf0 | ((u >> 18) & 0xff)));
      utf8.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3f)));
      utf8.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3f)));
      utf8.push_back(static_cast<char>(0x80 | (u & 0x3f)));
    }
    return utf8;
  }

  Json parseArray() {
    Json::array_t arr;
    ++curr_;  // skip '['
    parseWhitespace();
    if (*curr_ == ']') {
      start_ = ++curr_;
      return Json(arr);
    }
    while (1) {
      parseWhitespace();
      arr.push_back(parseValue());
      parseWhitespace();
      if (*curr_ == ',')
        ++curr_;
      else if (*curr_ == ']') {
        start_ = ++curr_;
        return Json(arr);
      } else
        error("MISS COMMA OR SQUARE BRACKET");
    }
  }

  Json parseObject() {
    Json::object_t obj;
    ++curr_;
    parseWhitespace();
    if (*curr_ == '}') {
      start_ = ++curr_;
      return Json(obj);
    }
    while (1) {
      parseWhitespace();
      if (*curr_ != '"') error("MISS KEY");
      string key = parseRawString();
      parseWhitespace();
      if (*curr_++ != ':') error("MISS COLON");
      parseWhitespace();
      Json val = parseValue();
      // obj[key] = val;
      obj.insert({key, val});
      parseWhitespace();
      if (*curr_ == ',')
        ++curr_;
      else if (*curr_ == '}') {
        start_ = ++curr_;
        return Json(obj);
      } else
        error("MISS COMMA OR CURLY BRACKET");
    }
  }

  void parseWhitespace() {
    while (*curr_ == ' ' || *curr_ == '\t' || *curr_ == '\r' || *curr_ == '\n')
      ++curr_;
    start_ = curr_;
  }

  [[noreturn]] void error(const string& msg) const {
    throw runtime_error(msg + ": " + start_);
  }

  const char* start_;
  const char* curr_;
};
}  // namespace details

namespace json {
Json::Json(nullptr_t) : value_(make_unique<JsonNull>()) {}
Json::Json(bool val) : value_(make_unique<JsonBool>(val)) {}
Json::Json(int val) : Json(val * 1.0) {}
Json::Json(double val) : value_(make_unique<JsonNumber>(val)) {}
Json::Json(const string& val) : value_(make_unique<JsonString>(val)) {}
Json::Json(const char* val) : value_(make_unique<JsonString>(val)) {}
Json::Json(const array_t& val) : value_(make_unique<JsonArray>(val)) {}
Json::Json(const object_t& val) : value_(make_unique<JsonObject>(val)) {}
Json::Json(const Json& rhs) {
  switch (rhs.type()) {
    case JsonType::kNull:
      value_ = make_unique<JsonNull>();
      break;
    case JsonType::kBool:
      value_ = make_unique<JsonBool>(rhs.asBool());
      break;
    case JsonType::kNumber:
      value_ = make_unique<JsonNumber>(rhs.asDouble());
      break;
    case JsonType::kString:
      value_ = make_unique<JsonString>(rhs.asString());
      break;
    case JsonType::kArray:
      value_ = make_unique<JsonArray>(rhs.asArray());
      break;
    case JsonType::kObject:
      value_ = make_unique<JsonObject>(rhs.asObect());
      break;
  }
}
Json::Json(Json&& rhs) noexcept : value_(std::move(rhs.value_)) {}
Json::~Json() {}

Json& Json::operator=(Json rhs) {
  swap(rhs);
  return *this;
}
void Json::swap(Json& rhs) noexcept {
  using std::swap;
  swap(value_, rhs.value_);
}

bool Json::operator==(const Json& rhs) const {
  if (value_.get() == rhs.value_.get()) return true;
  if (type() != rhs.type()) return false;

  switch (type()) {
    case JsonType::kNull:
      return true;
    case JsonType::kBool:
      return asBool() == rhs.asBool();
    case JsonType::kNumber:
      return asDouble() == rhs.asDouble();
    case JsonType::kString:
      return asString() == rhs.asString();
    case JsonType::kArray:
      return asArray() == rhs.asArray();
    case JsonType::kObject:
      return asObect() == rhs.asObect();
  }
  assert(0);
}
bool Json::operator!=(const Json& rhs) const { return !(*this == rhs); }

Json::JsonType Json::type() const { return value_->type(); }
bool Json::isNull() const { return type() == JsonType::kNull; }
bool Json::isBool() const { return type() == JsonType::kBool; }
bool Json::isNumber() const { return type() == JsonType::kNumber; }
bool Json::isString() const { return type() == JsonType::kString; }
bool Json::isArray() const { return type() == JsonType::kArray; }
bool Json::isObject() const { return type() == JsonType::kObject; }

bool Json::asBool() const { return value_->asBool(); }
double Json::asDouble() const { return value_->asDouble(); }
const string& Json::asString() const { return value_->asString(); }
Json::array_t& Json::asArray() { return value_->asArray(); }
const Json::array_t& Json::asArray() const { return value_->asArray(); }
Json::object_t& Json::asObect() { return value_->asObect(); }
const Json::object_t& Json::asObect() const { return value_->asObect(); }

Json& Json::operator[](size_t i) { return value_->operator[](i); }
const Json& Json::operator[](size_t i) const { return value_->operator[](i); }
Json& Json::operator[](const string& i) { return value_->operator[](i); }
const Json& Json::operator[](const string& i) const {
  return value_->operator[](i);
}

size_t Json::size() const { return value_->size(); }

Json Json::parse(const string& content, string& errMsg) {
  try {
    Parser p(content);
    return p.parse();
  } catch (runtime_error& e) {
    errMsg = e.what();
    return Json(nullptr);
  }
}

string Json::serialize() const {
  switch (value_->type()) {
    case Json::JsonType::kNull:
      return "null";
    case Json::JsonType::kBool:
      return value_->asBool() ? "true" : "false";
    case Json::JsonType::kNumber:
      char buf[32];
      sprintf(buf, "%.17g", value_->asDouble());
      return buf;
    case Json::JsonType::kString:
      return serializeString();
    case Json::JsonType::kArray:
      return serializeArray();
    case Json::JsonType::kObject:
      return serializeObject();
  }
  assert(0);
}

string Json::serializeString() const {
  string ret = "\"";
  for (auto e : value_->asString()) {
    switch (e) {
      case '\"':
        ret += "\\\"";
        break;
      case '\\':
        ret += "\\\\";
        break;
      case '\b':
        ret += "\\b";
        break;
      case '\f':
        ret += "\\f";
        break;
      case '\n':
        ret += "\\n";
        break;
      case '\r':
        ret += "\\r";
        break;
      case '\t':
        ret += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(e) < 0x20) {
          char buf[7];
          sprintf(buf, "\\u%04X", e);
          ret += buf;
        } else
          ret += e;
    }
  }
  return ret + '"';
}

string Json::serializeArray() const {
  string ret = "[ ";
  for (size_t i = 0; i < value_->size(); ++i) {
    if (i > 0) ret += ", ";
    ret += (*this)[i].serialize();
  }
  return ret + " ]";
}

string Json::serializeObject() const {
  string ret = "{ ";
  bool first = 1;
  for (const pair<string, Json>& p : value_->asObect()) {
    if (first)
      first = 0;
    else
      ret += ", ";

    ret += "\"" + p.first + "\"";
    ret += ": ";
    ret += p.second.serialize();
  }
  return ret + " }";
}

}  // namespace json