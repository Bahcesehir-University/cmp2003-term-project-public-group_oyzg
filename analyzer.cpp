#include "analyzer.h"

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <cctype>

// ================= shared storage =================
static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, long long>> zoneCount;
static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, std::array<long long, 24>>> zoneHourCount;

// ---------------- helpers ----------------
static inline void stripCR(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

static inline bool isHeader(const std::string& line) {
    return line.rfind("TripID", 0) == 0;
}

static inline bool parseHourLoose(const std::string& s, size_t start, size_t end, int& hour) {
    for (size_t i = start; i + 1 < end; ++i) {
        if (std::isdigit(s[i]) && std::isdigit(s[i + 1])) {
            int h = (s[i] - '0') * 10 + (s[i + 1] - '0');
            if (h >= 0 && h <= 23) {
                hour = h;
                return true;
            }
        }
    }
    return false;
}

// ================= implementation =================
void TripAnalyzer::ingestFile(const std::string& csvPath) {
    auto& zc = zoneCount[this];
    auto& zh = zoneHourCount[this];

    zc.clear();
    zh.clear();

    std::ifstream in(csvPath);
    if (!in.is_open()) return;

    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        stripCR(line);
        if (line.empty()) continue;

        if (first) {
            first = false;
            if (isHeader(line)) continue;
        }

        // find needed commas only
        size_t c1 = line.find(',');
        if (c1 == std::string::npos) continue;
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        size_t c3 = line.find(',', c2 + 1);
        if (c3 == std::string::npos) continue;
        size_t c4 = line.find(',', c3 + 1);
        if (c4 == std::string::npos) continue;

        // zone
        std::string zone = line.substr(c1 + 1, c2 - c1 - 1);
        if (zone.empty()) continue;

        // datetime
        int hour;
        if (!parseHourLoose(line, c3 + 1, c4, hour)) continue;

        zc[zone]++;
        auto& arr = zh[zone];
        arr[hour]++;
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    if (k <= 0) return {};
    auto it = zoneCount.find(this);
    if (it == zoneCount.end()) return {};

    std::vector<ZoneCount> v;
    for (auto& p : it->second)
        v.push_back({p.first, p.second});

    std::sort(v.begin(), v.end(), [](auto& a, auto& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    });

    if ((int)v.size() > k) v.resize(k);
    return v;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (k <= 0) return {};
    auto it = zoneHourCount.find(this);
    if (it == zoneHourCount.end()) return {};

    std::vector<SlotCount> v;
    for (auto& z : it->second) {
        for (int h = 0; h < 24; ++h) {
            if (z.second[h] > 0)
                v.push_back({z.first, h, z.second[h]});
        }
    }

    std::sort(v.begin(), v.end(), [](auto& a, auto& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    });

    if ((int)v.size() > k) v.resize(k);
    return v;
}
