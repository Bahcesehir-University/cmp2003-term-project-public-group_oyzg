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

// case-insensitive "TripID" header detection (after trimming)
static inline bool isHeaderLine(std::string line) {
    trimInPlace(line);
    if (line.empty()) return false;

    // take first token until comma
    size_t c = line.find(',');
    std::string first = (c == std::string::npos) ? line : line.substr(0, c);
    trimInPlace(first);
    toUpperInPlace(first);
    return (first == "TRIPID");
}

// Parse hour from a datetime string robustly.
// Accepts: "YYYY-MM-DD HH:MM", "YYYY-MM-DDTHH:MM", "YYYY-MM-DD HH:MM:SS", etc.
// Strategy: find first ':' and read two digits immediately before it.
static inline bool parseHourFromDatetime(std::string dt, int& hourOut) {
    trimInPlace(dt);
    if (dt.size() < 4) return false;

    // Find a ':' that belongs to time
    size_t colon = dt.find(':');
    if (colon == std::string::npos) return false;
    if (colon < 2) return false;

    char h1 = dt[colon - 2];
    char h2 = dt[colon - 1];
    if (!std::isdigit((unsigned char)h1) || !std::isdigit((unsigned char)h2)) return false;

    int h = (h1 - '0') * 10 + (h2 - '0');
    if (h < 0 || h > 23) return false;

    hourOut = h;
    return true;
}

// Get next field [pos..comma) and advance pos to after comma.
// Returns false if no more data.
// If allowEnd = true, returns last field until end.
static inline bool readField(const std::string& line, size_t& pos, std::string& out, bool allowEnd = true) {
    if (pos > line.size()) return false;
    size_t comma = line.find(',', pos);
    if (comma == std::string::npos) {
        if (!allowEnd) return false;
        out = line.substr(pos);
        pos = line.size() + 1;
        return true;
    }
    out = line.substr(pos, comma - pos);
    pos = comma + 1;
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
    bool firstLineChecked = false;

    while (std::getline(in, line)) {
        stripCR(line);
        if (line.empty()) continue;

        // skip header (case-insensitive)
        if (!firstLineChecked) {
            firstLineChecked = true;
            if (isHeaderLine(line)) continue;
        } else {
            // also skip accidental header lines inside dirty data
            if (isHeaderLine(line)) continue;
        }

        // We want at least: TripID, PickupZoneID, DropoffZoneID, PickupDateTime, TripDistance, FareAmount
        // BUT: for dirty data we will accept extra columns and ignore them.
        size_t pos = 0;
        std::string f1, pickupZone, f3, pickupDT, f5, f6;

        if (!readField(line, pos, f1, false)) continue;           // TripID
        if (!readField(line, pos, pickupZone, false)) continue;   // PickupZoneID
        if (!readField(line, pos, f3, false)) continue;           // DropoffZoneID
        if (!readField(line, pos, pickupDT, false)) continue;     // PickupDateTime
        if (!readField(line, pos, f5, false)) continue;           // TripDistance
        if (!readField(line, pos, f6, true))  continue;           // FareAmount (or rest)

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
    v.reserve(zoneHour.size()); // will grow but fine

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
