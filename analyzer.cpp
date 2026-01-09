#include <string>
#include <vector>
#include <cctype>

// Split by comma (simple CSV, no quoted commas expected in this assignment)
static inline void splitCSV6(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    out.reserve(6);

    size_t start = 0;
    for (int i = 0; i < 5; i++) {
        size_t pos = line.find(',', start);
        if (pos == std::string::npos) { out.clear(); return; }
        out.emplace_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    out.emplace_back(line.substr(start));

    // If more than 6 fields, it's dirty (extra commas)
    // We detect it by checking if last field contains a comma
    if (out.size() != 6 || out[5].find(',') != std::string::npos) out.clear();
}

static inline bool isHeaderLine(const std::string& line) {
    // Your dataset header usually starts with TripID (or similar)
    // Keep it strict to avoid skipping real data.
    return line.rfind("TripID", 0) == 0;
}

// Extract hour from "... YYYY-MM-DD HH:MM ..." format
static inline bool parseHourFromTimestamp(const std::string& ts, int& hourOut) {
    // We expect a space between date and time, then HH:MM...
    size_t sp = ts.find(' ');
    if (sp == std::string::npos) return false;
    if (sp + 3 >= ts.size()) return false;

    char h1 = ts[sp + 1];
    char h2 = ts[sp + 2];
    if (!std::isdigit((unsigned char)h1) || !std::isdigit((unsigned char)h2)) return false;

    int h = (h1 - '0') * 10 + (h2 - '0');
    if (h < 0 || h > 23) return false;

    // Optional: validate colon after HH
    if (sp + 3 >= ts.size() || ts[sp + 3] != ':') return false;

    hourOut = h;
    return true;
}
