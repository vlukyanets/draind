#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace json {

struct Value;
using Array  = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object> v;

    Value() : v(nullptr) {}
    Value(std::nullptr_t) : v(nullptr) {}
    Value(bool b) : v(b) {}
    Value(int n) : v((int64_t)n) {}
    Value(int64_t n) : v(n) {}
    Value(double d) : v(d) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(Array a) : v(std::move(a)) {}
    Value(Object o) : v(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool is_bool() const { return std::holds_alternative<bool>(v); }
    bool is_int() const { return std::holds_alternative<int64_t>(v); }
    bool is_double() const { return std::holds_alternative<double>(v); }
    bool is_string() const { return std::holds_alternative<std::string>(v); }
    bool is_array() const { return std::holds_alternative<Array>(v); }
    bool is_object() const { return std::holds_alternative<Object>(v); }

    bool get_bool(bool def = false) const {
        if (auto* p = std::get_if<bool>(&v))
            return *p;
        return def;
    }
    int64_t get_int(int64_t def = 0) const {
        if (auto* p = std::get_if<int64_t>(&v))
            return *p;
        if (auto* p = std::get_if<double>(&v))
            return (int64_t)*p;
        return def;
    }
    const std::string& get_string() const {
        static const std::string empty;
        if (auto* p = std::get_if<std::string>(&v))
            return *p;
        return empty;
    }
    const Array& get_array() const {
        static const Array empty;
        if (auto* p = std::get_if<Array>(&v))
            return *p;
        return empty;
    }
    const Object& get_object() const {
        static const Object empty;
        if (auto* p = std::get_if<Object>(&v))
            return *p;
        return empty;
    }

    bool has(const std::string& key) const {
        if (auto* o = std::get_if<Object>(&v))
            return o->count(key) > 0;
        return false;
    }
    const Value& operator[](const std::string& key) const {
        static const Value null_val;
        if (auto* o = std::get_if<Object>(&v)) {
            auto it = o->find(key);
            if (it != o->end())
                return it->second;
        }
        return null_val;
    }
    Value& operator[](const std::string& key) {
        if (!is_object())
            v = Object{};
        return std::get<Object>(v)[key];
    }

    // Convenience accessors for object fields
    std::string str(const std::string& key, const std::string& def = "") const {
        if (!has(key))
            return def;
        return (*this)[key].get_string();
    }
    int64_t num(const std::string& key, int64_t def = 0) const {
        if (!has(key))
            return def;
        return (*this)[key].get_int(def);
    }
    bool flag(const std::string& key, bool def = false) const {
        if (!has(key))
            return def;
        return (*this)[key].get_bool(def);
    }

    void push(Value val) {
        if (!is_array())
            v = Array{};
        std::get<Array>(v).push_back(std::move(val));
    }
};

// ── Parser ────────────────────────────────────────────────────────────────────

namespace detail {

struct Parser {
    const char* p;
    const char* end;

    explicit Parser(std::string_view s) : p(s.data()), end(s.data() + s.size()) {}

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
    }
    char peek() const { return p < end ? *p : '\0'; }

    std::string parse_string() {
        if (peek() != '"')
            throw std::runtime_error("json: expected '\"'");
        ++p;
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (p >= end)
                    break;
                switch (*p++) {
                case '"':
                    s += '"';
                    break;
                case '\\':
                    s += '\\';
                    break;
                case '/':
                    s += '/';
                    break;
                case 'n':
                    s += '\n';
                    break;
                case 'r':
                    s += '\r';
                    break;
                case 't':
                    s += '\t';
                    break;
                default:
                    s += *(p - 1);
                    break;
                }
            } else {
                s += *p++;
            }
        }
        if (p >= end || *p != '"')
            throw std::runtime_error("json: unterminated string");
        ++p;
        return s;
    }

    Value parse_number() {
        const char* start    = p;
        bool        is_float = false;
        if (peek() == '-')
            ++p;
        while (p < end && *p >= '0' && *p <= '9')
            ++p;
        if (p < end && *p == '.') {
            is_float = true;
            ++p;
            while (p < end && *p >= '0' && *p <= '9')
                ++p;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            is_float = true;
            ++p;
            if (p < end && (*p == '+' || *p == '-'))
                ++p;
            while (p < end && *p >= '0' && *p <= '9')
                ++p;
        }
        std::string s(start, p);
        if (is_float)
            return Value(std::stod(s));
        return Value((int64_t)std::stoll(s));
    }

    Value parse_value() {
        skip_ws();
        if (p >= end)
            throw std::runtime_error("json: unexpected end");
        char c = peek();
        if (c == '"')
            return Value(parse_string());
        if (c == '{')
            return parse_object();
        if (c == '[')
            return parse_array();
        if (c == 't') {
            p += 4;
            return Value(true);
        }
        if (c == 'f') {
            p += 5;
            return Value(false);
        }
        if (c == 'n') {
            p += 4;
            return Value(nullptr);
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
        throw std::runtime_error(std::string("json: unexpected char: ") + c);
    }

    Value parse_object() {
        ++p; // '{'
        Object obj;
        skip_ws();
        if (peek() == '}') {
            ++p;
            return Value(std::move(obj));
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (peek() != ':')
                throw std::runtime_error("json: expected ':'");
            ++p;
            obj[key] = parse_value();
            skip_ws();
            if (peek() == '}') {
                ++p;
                break;
            }
            if (peek() != ',')
                throw std::runtime_error("json: expected ',' or '}'");
            ++p;
        }
        return Value(std::move(obj));
    }

    Value parse_array() {
        ++p; // '['
        Array arr;
        skip_ws();
        if (peek() == ']') {
            ++p;
            return Value(std::move(arr));
        }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            if (peek() == ']') {
                ++p;
                break;
            }
            if (peek() != ',')
                throw std::runtime_error("json: expected ',' or ']'");
            ++p;
        }
        return Value(std::move(arr));
    }
};

inline std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    out += '"';
    return out;
}

} // namespace detail

inline Value parse(std::string_view s) {
    detail::Parser p(s);
    return p.parse_value();
}

inline std::string dump(const Value& val) {
    if (val.is_null())
        return "null";
    if (val.is_bool())
        return val.get_bool() ? "true" : "false";
    if (val.is_int())
        return std::to_string(val.get_int());
    if (val.is_double()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", std::get<double>(val.v));
        return buf;
    }
    if (val.is_string())
        return detail::escape(val.get_string());
    if (val.is_array()) {
        std::string s     = "[";
        bool        first = true;
        for (const auto& e : val.get_array()) {
            if (!first)
                s += ',';
            s += dump(e);
            first = false;
        }
        return s + ']';
    }
    if (val.is_object()) {
        std::string s     = "{";
        bool        first = true;
        for (const auto& [k, e] : val.get_object()) {
            if (!first)
                s += ',';
            s += detail::escape(k);
            s += ':';
            s += dump(e);
            first = false;
        }
        return s + '}';
    }
    return "null";
}

namespace detail {
inline void dump_pretty(const Value& val, std::string& out, int indent, int depth) {
    const std::string pad(depth * indent, ' ');
    const std::string pad1((depth + 1) * indent, ' ');

    if (val.is_array()) {
        const auto& arr = val.get_array();
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (size_t i = 0; i < arr.size(); ++i) {
            out += pad1;
            dump_pretty(arr[i], out, indent, depth + 1);
            if (i + 1 < arr.size())
                out += ',';
            out += '\n';
        }
        out += pad + ']';
        return;
    }
    if (val.is_object()) {
        const auto& obj = val.get_object();
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        size_t i = 0;
        for (const auto& [k, v] : obj) {
            out += pad1 + escape(k) + ": ";
            dump_pretty(v, out, indent, depth + 1);
            if (++i < obj.size())
                out += ',';
            out += '\n';
        }
        out += pad + '}';
        return;
    }
    out += dump(val);
}
} // namespace detail

inline std::string dump_pretty(const Value& val, int indent = 2) {
    std::string out;
    detail::dump_pretty(val, out, indent, 0);
    out += '\n';
    return out;
}

} // namespace json
