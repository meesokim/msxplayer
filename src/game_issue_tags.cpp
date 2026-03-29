#include "game_issue_tags.h"

#include <cctype>
#include <fstream>

static std::string normalizeSha1Line(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
    for (size_t i = 0; i < s.size(); i++)
        s[i] = (char)std::tolower((unsigned char)s[i]);
    if (s.size() != 40) return "";
    for (char c : s) {
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return "";
    }
    return s;
}

bool GameIssueTags::load(const std::string& path) {
    path_ = path;
    sha1s_.clear();
    std::ifstream in(path);
    if (!in)
        return true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t p = line.find_first_not_of(" \t\r\n");
        if (p != std::string::npos && line[p] == '#') continue;
        std::string k = normalizeSha1Line(line);
        if (!k.empty()) sha1s_.insert(k);
    }
    return true;
}

bool GameIssueTags::contains(const std::string& sha1Hex40) const {
    std::string k = normalizeSha1Line(sha1Hex40);
    return !k.empty() && sha1s_.find(k) != sha1s_.end();
}

bool GameIssueTags::add(const std::string& sha1Hex40) {
    std::string k = normalizeSha1Line(sha1Hex40);
    if (k.empty()) return false;
    if (sha1s_.count(k)) return false;
    std::ofstream out(path_, std::ios::app);
    if (!out) return false;
    out << k << '\n';
    if (!out) return false;
    sha1s_.insert(std::move(k));
    return true;
}

bool GameIssueTags::remove(const std::string& sha1Hex40) {
    std::string k = normalizeSha1Line(sha1Hex40);
    if (k.empty() || sha1s_.find(k) == sha1s_.end()) return false;
    std::unordered_set<std::string> backup = sha1s_;
    sha1s_.erase(k);
    std::ofstream out(path_, std::ios::trunc);
    if (!out) {
        sha1s_ = std::move(backup);
        return false;
    }
    for (const std::string& s : sha1s_)
        out << s << '\n';
    if (!out) {
        sha1s_ = std::move(backup);
        return false;
    }
    return true;
}
