#include "analyzer.h"

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <cctype>

// ============================================================
// Shared state (because analyzer.h has no private members)
// Keyed by TripAnalyzer instance pointer.
// ============================================================
static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, long long>> g_zoneTrips;
static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, std::array<long long, 24>>> g_zoneHourTrips;

// ------------------- helpers -------------------

static inline void stripCR(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

static inline void trimInPlace(std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
    size_t e = s.size();
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    if (b == 0 && e == s.size()) return;
    s.assign(s.begin() + (long)b, s.begin() + (long)e);
}

static inline void toUpperInPlace(std::string& s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
}

static std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    size_t i = 0;
    std::string field;
    bool inQuotes = false;

    while (i < line.size()) {
        char ch = line[i];

        if (!inQuotes && ch == ',') {
            fields.push_back(field);
            field.clear();
            ++i;
            continue;
        }

        if (ch == '"') {
            if (!inQuotes) {
                inQuotes = true;
                ++i;
                continue;
            } else {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    i += 2;
                    continue;
                } else {
                    inQuotes = false;
                    ++i;
                    continue;
                }
            }
        }

        field += ch;
        ++i;
    }

    if (!field.empty() || inQuotes) {
        fields.push_back(field);
    }

    return fields;
}

// case-insensitive "TripID" header detection (after trimming)
static inline bool isHeaderLine(const std::string& line) {
    auto fields = parseCSVLine(line);
    if (fields.empty()) return false;

    std::string first = fields[0];
    trimInPlace(first);
    toUpperInPlace(first);
    return (first == "TRIPID");
}

// Parse hour from a datetime string robustly.
// Accepts: "YYYY-MM-DD HH:MM", "YYYY-MM-DDTHH:MM", "YYYY-MM-DD HH:MM:SS", etc.
// Strategy: find first ':' and extract the 1-2 digit hour immediately before it.
static inline bool parseHourFromDatetime(std::string dt, int& hourOut) {
    trimInPlace(dt);
    if (dt.empty()) return false;

    size_t colon = dt.find(':');
    if (colon == std::string::npos) return false;

    size_t end = colon - 1;
    if (end >= dt.size()) return false; // invalid
    size_t start = end;
    while (start > 0 && std::isdigit((unsigned char)dt[start - 1])) --start;

    std::string hStr = dt.substr(start, end - start + 1);
    if (hStr.empty() || hStr.size() > 2) return false;

    if (!std::all_of(hStr.begin(), hStr.end(), [](char c){ return std::isdigit((unsigned char)c); })) return false;

    int h = std::stoi(hStr);

    std::string dtUpper = dt;
    toUpperInPlace(dtUpper);
    bool hasAm = dtUpper.find("AM") != std::string::npos;
    bool hasPm = dtUpper.find("PM") != std::string::npos;

    if (hasAm || hasPm) {
        // 12-hour format
        if (h < 1 || h > 12) return false;
        if (hasPm) {
            if (h != 12) h += 12;
        } else { // AM
            if (h == 12) h = 0;
        }
    } else {
        // 24-hour format
        if (h < 0 || h > 23) return false;
    }

    hourOut = h;
    return true;
}

// ------------------- TripAnalyzer implementation -------------------

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    auto& zoneTrips = g_zoneTrips[this];
    auto& zoneHour  = g_zoneHourTrips[this];

    zoneTrips.clear();
    zoneHour.clear();

    std::ifstream in(csvPath);
    if (!in.is_open()) {
        // Requirement: never crash; missing file => empty result
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        stripCR(line);
        if (line.empty()) continue;

        // skip header (case-insensitive)
        if (isHeaderLine(line)) continue;

        auto fields = parseCSVLine(line);
        if (fields.size() < 6) continue;

        std::string pickupZone = fields[1];
        std::string pickupDT = fields[3];

        trimInPlace(pickupZone);
        if (pickupZone.empty()) continue;

        // case-insensitivity requirement: normalize zone ids
        toUpperInPlace(pickupZone);

        trimInPlace(pickupDT);
        int hour = -1;
        if (!parseHourFromDatetime(pickupDT, hour)) continue;

        // Update totals
        zoneTrips[pickupZone]++;

        auto it = zoneHour.find(pickupZone);
        if (it == zoneHour.end()) {
            std::array<long long, 24> arr{};
            arr.fill(0);
            arr[hour] = 1;
            zoneHour.emplace(pickupZone, arr);
        } else {
            it->second[hour]++;
        }
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    if (k <= 0) return {};

    auto itObj = g_zoneTrips.find(this);
    if (itObj == g_zoneTrips.end()) return {};

    const auto& zoneTrips = itObj->second;

    std::vector<ZoneCount> v;
    v.reserve(zoneTrips.size());
    for (const auto& kv : zoneTrips) {
        v.push_back({kv.first, kv.second});
    }

    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count; // count desc
        return a.zone < b.zone;                           // zone asc
    };

    if ((int)v.size() > k) {
        std::partial_sort(v.begin(), v.begin() + k, v.end(), cmp);
        v.resize(k);
    } else {
        std::sort(v.begin(), v.end(), cmp);
    }
    return v;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    if (k <= 0) return {};

    auto itObj = g_zoneHourTrips.find(this);
    if (itObj == g_zoneHourTrips.end()) return {};

    const auto& zoneHour = itObj->second;

    std::vector<SlotCount> v;
    v.reserve(zoneHour.size() * 24 / 2); // rough estimate

    for (const auto& kv : zoneHour) {
        const std::string& zone = kv.first;
        const auto& arr = kv.second;
        for (int h = 0; h < 24; ++h) {
            long long cnt = arr[h];
            if (cnt > 0) v.push_back({zone, h, cnt});
        }
    }

    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count; // count desc
        if (a.zone != b.zone) return a.zone < b.zone;     // zone asc
        return a.hour < b.hour;                           // hour asc
    };

    if ((int)v.size() > k) {
        std::partial_sort(v.begin(), v.begin() + k, v.end(), cmp);
        v.resize(k);
    } else {
        std::sort(v.begin(), v.end(), cmp);
    }
    return v;
}
