#ifndef GAME_ISSUE_TAGS_H
#define GAME_ISSUE_TAGS_H

#include <string>
#include <unordered_set>

/** Persistent set of ROM SHA1 (hex) marked as having known issues; see game_issue_tags.txt */
class GameIssueTags {
public:
    bool load(const std::string& path);
    const std::string& path() const { return path_; }
    bool empty() const { return sha1s_.empty(); }
    bool contains(const std::string& sha1Hex40) const;
    /** Appends to file and updates in-memory set. Returns false if invalid SHA1 or I/O failure (set unchanged on I/O fail). */
    bool add(const std::string& sha1Hex40);
    /** Removes from set and rewrites tag file. Returns false if not marked or rewrite fails. */
    bool remove(const std::string& sha1Hex40);

private:
    std::string path_;
    std::unordered_set<std::string> sha1s_;
};

#endif
