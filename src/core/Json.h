// VRChat DLSS5 Cam - a small JSON value with a reader and a writer: the release list of the updater, the MCP server.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace vdc {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    Json() = default;
    Json(bool v) : type(Bool), b(v) {}
    Json(int v) : type(Number), num(v) {}
    Json(unsigned v) : type(Number), num(v) {}
    Json(long long v) : type(Number), num((double)v) {}
    Json(unsigned long long v) : type(Number), num((double)v) {}
    Json(double v) : type(Number), num(v) {}
    Json(float v) : type(Number), num(v) {}
    Json(const char* v) : type(String), str(v ? v : "") {}
    Json(const std::string& v) : type(String), str(v) {}
    Json(std::string&& v) : type(String), str(std::move(v)) {}
    static Json Obj() { Json j; j.type = Json::Object; return j; }
    static Json Arr() { Json j; j.type = Json::Array; return j; }

    // Reading.
    const Json* Find(const char* key) const {
        if (type != Object) return nullptr;
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool Has(const char* key) const { return Find(key) != nullptr; }
    std::string Str(const char* key, const char* def = "") const { const Json* j = Find(key); return j && j->type == String ? j->str : std::string(def); }
    bool Flag(const char* key, bool def = false) const {
        const Json* j = Find(key);
        if (!j) return def;
        if (j->type == Bool) return j->b;
        if (j->type == Number) return j->num != 0.0;
        if (j->type == String) return j->str == "1" || j->str == "true";
        return def;
    }
    double Num(const char* key, double def = 0.0) const {
        const Json* j = Find(key);
        if (!j) return def;
        if (j->type == Number) return j->num;
        if (j->type == Bool) return j->b ? 1.0 : 0.0;
        if (j->type == String && !j->str.empty()) { char* e = nullptr; const double v = strtod(j->str.c_str(), &e); return e && *e == 0 ? v : def; }
        return def;
    }
    int Int(const char* key, int def = 0) const { return (int)std::lround(Num(key, def)); }
    // A value of any scalar type as text (what a setting takes).
    std::string Text() const {
        switch (type) {
        case Bool: return b ? "1" : "0";
        case Number: return NumberText(num);
        case String: return str;
        default: return std::string();
        }
    }

    // Building.
    Json& Set(const char* key, Json v) {
        type = Object;
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return *this; }
        obj.emplace_back(key, std::move(v));
        return *this;
    }
    Json& Push(Json v) { type = Array; arr.push_back(std::move(v)); return *this; }

    // Writing.
    static std::string NumberText(double v) {
        if (std::isnan(v) || std::isinf(v)) return "null";
        if (std::fabs(v) < 1e15 && v == std::floor(v)) { char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)v); return buf; }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.10g", v);
        return buf;
    }
    static void Escape(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
                else out += (char)c;
            }
        }
        out += '"';
    }
    void Dump(std::string& out, int indent = -1, int depth = 0) const {
        auto newline = [&](int d) { if (indent >= 0) { out += '\n'; out.append((size_t)(d * indent), ' '); } };
        switch (type) {
        case Null: out += "null"; break;
        case Bool: out += b ? "true" : "false"; break;
        case Number: out += NumberText(num); break;
        case String: Escape(out, str); break;
        case Array:
            out += '[';
            for (size_t i = 0; i < arr.size(); ++i) { if (i) out += ','; newline(depth + 1); arr[i].Dump(out, indent, depth + 1); }
            if (!arr.empty()) newline(depth);
            out += ']';
            break;
        case Object:
            out += '{';
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                Escape(out, obj[i].first);
                out += indent >= 0 ? ": " : ":";
                obj[i].second.Dump(out, indent, depth + 1);
            }
            if (!obj.empty()) newline(depth);
            out += '}';
            break;
        }
    }
    std::string Dump(int indent = -1) const { std::string s; Dump(s, indent); return s; }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : m_s(text) {}
    bool Parse(Json& out) { Skip(); if (!Value(out, 0)) return false; Skip(); return m_i == m_s.size(); }
    static bool Parse(const std::string& text, Json& out) { return JsonReader(text).Parse(out); }
private:
    void Skip() { while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r')) ++m_i; }
    bool Lit(const char* w) { const size_t n = strlen(w); if (m_s.compare(m_i, n, w) != 0) return false; m_i += n; return true; }
    static void PutUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(unsigned& v) {
        if (m_i + 4 > m_s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = m_s[m_i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool Text(std::string& out) {
        if (m_i >= m_s.size() || m_s[m_i] != '"') return false;
        ++m_i;
        while (m_i < m_s.size()) {
            const char c = m_s[m_i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (m_i >= m_s.size()) return false;
            const char e = m_s[m_i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && m_i + 6 <= m_s.size() && m_s[m_i] == '\\' && m_s[m_i + 1] == 'u') {
                    m_i += 2;
                    unsigned lo = 0;
                    if (!Hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                PutUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Value(Json& out, int depth) {
        if (depth > 64 || m_i >= m_s.size()) return false;
        const char c = m_s[m_i];
        if (c == '{') {
            out.type = Json::Object; ++m_i; Skip();
            if (m_i < m_s.size() && m_s[m_i] == '}') { ++m_i; return true; }
            for (;;) {
                Skip();
                std::string key;
                if (!Text(key)) return false;
                Skip();
                if (m_i >= m_s.size() || m_s[m_i] != ':') return false;
                ++m_i; Skip();
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.obj.emplace_back(std::move(key), std::move(v));
                Skip();
                if (m_i >= m_s.size()) return false;
                if (m_s[m_i] == ',') { ++m_i; continue; }
                if (m_s[m_i] == '}') { ++m_i; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Json::Array; ++m_i; Skip();
            if (m_i < m_s.size() && m_s[m_i] == ']') { ++m_i; return true; }
            for (;;) {
                Skip();
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.arr.push_back(std::move(v));
                Skip();
                if (m_i >= m_s.size()) return false;
                if (m_s[m_i] == ',') { ++m_i; continue; }
                if (m_s[m_i] == ']') { ++m_i; return true; }
                return false;
            }
        }
        if (c == '"') { out.type = Json::String; return Text(out.str); }
        if (Lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (Lit("false")) { out.type = Json::Bool; out.b = false; return true; }
        if (Lit("null")) { out.type = Json::Null; return true; }
        const size_t start = m_i;
        while (m_i < m_s.size() && (isdigit((unsigned char)m_s[m_i]) || m_s[m_i] == '-' || m_s[m_i] == '+' || m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E')) ++m_i;
        if (m_i == start) return false;
        out.type = Json::Number;
        out.num = atof(m_s.substr(start, m_i - start).c_str());
        return true;
    }
    const std::string& m_s;
    size_t m_i = 0;
};

} // namespace vdc
