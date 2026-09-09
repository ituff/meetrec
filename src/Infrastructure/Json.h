// Virtual Screen Recorder - minimal JSON reader/writer (design.md §4.10).
//
// A compact, audited implementation is used instead of a large external
// dependency (explicitly allowed by design.md). It supports the JSON grammar
// subset used by settings, logs, and diagnostics files: objects, arrays,
// strings with escapes, numbers, booleans, and null. Object key order is
// preserved so unknown fields survive a load/save round trip (FR-091).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vsr::json {

class Value;
using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

enum class Type { Null, Boolean, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(std::nullptr_t) : type_(Type::Null) {}
    Value(bool b) : type_(Type::Boolean), bool_(b) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(int n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(Array a) : type_(Type::Array), array_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), object_(std::make_shared<Object>(std::move(o))) {}

    Type GetType() const noexcept { return type_; }
    bool IsNull() const noexcept { return type_ == Type::Null; }

    bool AsBool(bool fallback = false) const noexcept {
        return type_ == Type::Boolean ? bool_ : fallback;
    }
    double AsNumber(double fallback = 0.0) const noexcept {
        return type_ == Type::Number ? number_ : fallback;
    }
    std::int64_t AsInt(std::int64_t fallback = 0) const noexcept {
        return type_ == Type::Number ? static_cast<std::int64_t>(number_) : fallback;
    }
    const std::string& AsString() const noexcept { return string_; }
    const std::string& AsString(const std::string& fallback) const {
        return type_ == Type::String ? string_ : fallback;
    }

    const Array& GetArray() const noexcept {
        static const Array kEmpty;
        return array_ ? *array_ : kEmpty;
    }
    const Object& AsObject() const noexcept {
        static const Object kEmpty;
        return object_ ? *object_ : kEmpty;
    }

    // Object access. Missing keys return a Null value.
    const Value& Find(const std::string& key) const noexcept;
    // Appends or replaces a key, preserving insertion order.
    void Set(std::string key, Value value);
    // Appends to an array value (converts the value to an array if empty).
    void Add(Value value);

    std::string Serialize() const;

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

// Parses a UTF-8 JSON document. Returns a Null value on any syntax error.
Value Parse(const std::string& text, bool* ok = nullptr);

// Escapes a UTF-8 string as a JSON string literal (without quotes).
std::string EscapeString(const std::string& s);

} // namespace vsr::json
