#include "parser.h"
#include <cctype>

using namespace std;

static string trim_copy(const string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}

CommandLine Parser::parse(const string &cmd) {
    CommandLine cl;
    const char *p = cmd.c_str();
    size_t len = cmd.size();
    size_t i = 0;
    while (i < len) {
        while (i < len && isspace((unsigned char)p[i])) i++;
        if (i >= len) break;

        if (p[i] == '&') { cl.background = true; i++; continue; }

        if (p[i] == '<') {
            i++; while (i < len && isspace((unsigned char)p[i])) i++;
            if (i >= len) break;
            string token;
            if (p[i] == '"' || p[i] == '\'') {
                char q = p[i++]; while (i < len && p[i] != q) token.push_back(p[i++]); if (i < len && p[i] == q) i++;
            } else {
                while (i < len && !isspace((unsigned char)p[i]) && p[i] != '<' && p[i] != '>' && p[i] != '&') token.push_back(p[i++]);
            }
            cl.input_file = token; continue;
        }

        if (p[i] == '>') {
            i++; while (i < len && isspace((unsigned char)p[i])) i++;
            if (i >= len) break;
            string token;
            if (p[i] == '"' || p[i] == '\'') {
                char q = p[i++]; while (i < len && p[i] != q) token.push_back(p[i++]); if (i < len && p[i] == q) i++;
            } else {
                while (i < len && !isspace((unsigned char)p[i]) && p[i] != '<' && p[i] != '>' && p[i] != '&') token.push_back(p[i++]);
            }
            cl.output_file = token; continue;
        }

        string token;
        if (p[i] == '"' || p[i] == '\'') {
            char q = p[i++]; while (i < len && p[i] != q) token.push_back(p[i++]); if (i < len && p[i] == q) i++;
        } else {
            while (i < len && !isspace((unsigned char)p[i]) && p[i] != '<' && p[i] != '>' && p[i] != '&') token.push_back(p[i++]);
        }
        if (!token.empty()) cl.argv.push_back(token);
    }
    return cl;
}

vector<string> Parser::splitPipeline(const string &cmd) {
    vector<string> stages;
    string cur;
    char quote = 0;
    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (quote) {
            if (c == quote) quote = 0;
            cur.push_back(c);
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; cur.push_back(c); continue; }
        if (c == '|') {
            if (i + 1 < cmd.size() && cmd[i+1] == '|') { cur.push_back(c); continue; }
            string s = trim_copy(cur);
            if (!s.empty()) stages.push_back(s);
            cur.clear(); continue;
        }
        cur.push_back(c);
    }
    string s = trim_copy(cur);
    if (!s.empty()) stages.push_back(s);
    return stages;
}
