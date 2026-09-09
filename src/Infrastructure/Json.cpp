#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vsr::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Value ParseDocument(bool* ok) {
        Value v = ParseValue();
        SkipWs();
        const bool success = v.GetType() != Type::Null && !failed_ && pos_ >= text_.size();
        if (ok) *ok = success;
        return v;
    }

private:
    void SkipWs() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Consume(char c) {
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        failed_ = true;
        return false;
    }

    bool Match(char c) {
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    Value ParseValue() {
        SkipWs();
        if (pos_ >= text_.size()) {
            failed_ = true;
            return {};
        }
        const char c = text_[pos_];
        switch (c) {
            case '{': return ParseObject();
            case '[': return ParseArray();
            case '"': return Value(ParseString());
            case 't':
                return ParseLiteral("true", Value(true));
            case 'f':
                return ParseLiteral("false", Value(false));
            case 'n':
                return ParseLiteral("null", Value(nullptr));
            default:
                return ParseNumber();
        }
    }

    Value ParseLiteral(const char* literal, Value result) {
        const size_t len = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return result;
        }
        failed_ = true;
        return {};
    }

    Value ParseNumber() {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        bool digits = false;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '-' ||
                text_[pos_] == '+')) {
            if (std::isdigit(static_cast<unsigned char>(text_[pos_]))) digits = true;
            ++pos_;
        }
        if (!digits) {
            failed_ = true;
            return {};
        }
        const std::string slice = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double v = std::strtod(slice.c_str(), &end);
        if (end == nullptr || *end != '\0' || std::isnan(v)) {
            failed_ = true;
            return {};
        }
        return Value(v);
    }

    std::string ParseString() {
        std::string out;
        if (!Consume('"')) return out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned code = 0;
                    if (!ParseHex4(code)) { failed_ = true; return out; }
                    unsigned cp = code;
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low)) { failed_ = true; return out; }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            failed_ = true;
                            return out;
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    failed_ = true;
                    return out;
            }
        }
        failed_ = true;
        return out;
    }

    bool ParseHex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    Value ParseArray() {
        Array arr;
        if (!Consume('[')) return {};
        if (Match(']')) return Value(std::move(arr));
        for (;;) {
            arr.push_back(ParseValue());
            if (failed_) return {};
            if (Match(']')) return Value(std::move(arr));
            if (!Consume(',')) return {};
        }
    }

    Value ParseObject() {
        Object obj;
        if (!Consume('{')) return {};
        if (Match('}')) return Value(std::move(obj));
        for (;;) {
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != '"') { failed_ = true; return {}; }
            const std::string key = ParseString();
            if (failed_) return {};
            if (!Consume(':')) return {};
            Value v = ParseValue();
            if (failed_) return {};
            obj.emplace_back(key, std::move(v));
            if (Match('}')) return Value(std::move(obj));
            if (!Consume(',')) return {};
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
    bool failed_ = false;
};

} // namespace

const Value& Value::Find(const std::string& key) const noexcept {
    static const Value kNull;
    if (type_ != Type::Object || !object_) return kNull;
    for (const auto& kv : *object_) {
        if (kv.first == key) return kv.second;
    }
    return kNull;
}

void Value::Set(std::string key, Value value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_ = std::make_shared<Object>();
    }
    for (auto& kv : *object_) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    object_->emplace_back(std::move(key), std::move(value));
}

void Value::Add(Value value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_ = std::make_shared<Array>();
    }
    array_->push_back(std::move(value));
}

std::string EscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

namespace {

void SerializeNumber(double n, std::string& out) {
    if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 9.0e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        out += buf;
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", n);
        out += buf;
    }
}

void SerializeValue(const Value& v, std::string& out) {
    switch (v.GetType()) {
        case Type::Null: out += "null"; break;
        case Type::Boolean: out += v.AsBool() ? "true" : "false"; break;
        case Type::Number: SerializeNumber(v.AsNumber(), out); break;
        case Type::String:
            out.push_back('"');
            out += EscapeString(v.AsString());
            out.push_back('"');
            break;
        case Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : v.GetArray()) {
                if (!first) out.push_back(',');
                first = false;
                SerializeValue(item, out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.AsObject()) {
                if (!first) out.push_back(',');
                first = false;
                out.push_back('"');
                out += EscapeString(kv.first);
                out.push_back('"');
                out.push_back(':');
                SerializeValue(kv.second, out);
            }
            out.push_back('}');
            break;
        }
    }
}

} // namespace

std::string Value::Serialize() const {
    std::string out;
    SerializeValue(*this, out);
    return out;
}

Value Parse(const std::string& text, bool* ok) {
    Parser parser(text);
    return parser.ParseDocument(ok);
}

} // namespace vsr::json
