// trie.h -- prefix tree used for "starts with" search and autocomplete.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace unigo {

// Each edge is one character, so all names that start with "pri" share the path p -> r -> i.
// Finding them costs O(length of "pri") to walk down + O(number of matches) to collect,
// no matter how many places are stored.
class Trie {
    struct Node {
        std::map<char, std::unique_ptr<Node>> next;   // std::map keeps children sorted -> results come out A-Z
        std::vector<int> ids;                          // ids of places whose full name ends at this node
    };

public:
    Trie() : root_(new Node()) {}

    void insert(const std::string& key, int id) {
        Node* cur = root_.get();
        for (char c : key) {
            std::unique_ptr<Node>& child = cur->next[c];
            if (!child) child.reset(new Node());
            cur = child.get();
        }
        cur->ids.push_back(id);
    }

    std::vector<int> withPrefix(const std::string& prefix) const {
        const Node* cur = root_.get();
        for (char c : prefix) {
            auto it = cur->next.find(c);
            if (it == cur->next.end()) return {};      // no name starts with this prefix
            cur = it->second.get();
        }
        std::vector<int> out;
        collect(cur, out);
        return out;
    }

private:
    static void collect(const Node* n, std::vector<int>& out) {   // depth-first walk of the subtree
        out.insert(out.end(), n->ids.begin(), n->ids.end());
        for (const auto& kv : n->next) collect(kv.second.get(), out);
    }

    std::unique_ptr<Node> root_;   // unique_ptr: nodes are freed automatically, no manual delete
};

} // namespace unigo
