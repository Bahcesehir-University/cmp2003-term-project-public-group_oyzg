#include "analyzer.h"

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>

// --------- helpers (local only) ---------
static inline void stripCR(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

static inline bool isHeaderLine(const std::string& line) {
    // Accept common header start
    return line.rfind("TripID", 0) == 0;
}

// Parse hour from "YYYY-MM-DD HH:MM" inside [start, end)
static inline bool parseHour(const std::string& line, size_t start, size_t end, int& hourOut) {
    // find space separating date/time
    size_t sp = line.find(' ', start);
    if (sp == std::string::npos || sp + 2 >= end) return false;

    char h1 = line[sp + 1];
    char h2 = line[sp + 2];
    if (h1 < '0' || h1 > '9' || h2 < '0' || h2 > '9') return false;

    int h = (h1 - '0') * 10 + (h2 - '0');
    if (h < 0 || h > 23) return false;

    hourOut = h;
    return true;
}

// --------- TripAnalyzer implementation ---------

// Note: analyzer.h says "Parse Trips.csv, skip dirty rows, never crash"
void TripAnalyzer::ingestFile(const std::string& csvPath) {
    // Local state (static to this object) stored via function-local statics is NOT allowed,
    // so we store into member-like containers via static maps in this translation unit? No.
    // Instead, keep containers as static inside methods? also no.
    // Since analyzer.h doesn't define members, we store counts in static variables keyed by 'this'.
    // BUT that's messy. Better: use file-scope maps keyed by pointer. We'll avoid that by
    // using "pimpl" is not allowed either.
    //
    // ==> Therefore, we keep state in this .cpp file using two static unordered_maps keyed by this pointer.
    // This is a common workaround when headers are locked and contain no private members.

    static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, long long>> ZONE;
    static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, std::array<long long, 24>>> ZONEH;

    auto& zoneTrips = ZONE[this];
    auto& zoneHourTrips = ZONEH[this];

    zoneTrips.clear();
    zoneHourTrips.clear();

    std::ifstream in(csvPath);
    if (!in.is_open()) {
        // Missing file => empty results (matches robustness tests)
        return;
    }

    // Reserve for performance
    zoneTrips.reserve(200000);
    zoneHourTrips.reserve(200000);

    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        stripCR(line);
        if (line.empty()) continue;

        if (first) {
            first = false;
            if (isHeaderLine(line)) continue;
        }

        // Expected 6 columns => 5 commas
        size_t c1 = line.find(',');
        if (c1 == std::string::npos) continue;
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        size_t c3 = line.find(',', c2 + 1);
        if (c3 == std::string::npos) continue;
        size_t c4 = line.find(',', c3 + 1);
        if (c4 == std::string::npos) continue;
        size_t c5 = line.find(',', c4 + 1);
        if (c5 == std::string::npos) continue;

        // If there are extra commas/columns, treat as malformed to be safe
        if (line.find(',', c5 + 1) != std::string::npos) continue;

        // PickupZoneID = column 2 (between c1+1 and c2-1)
        if (c2 <= c1 + 1) continue; // empty pickup zone
        std::string zone = line.substr(c1 + 1, c2 - (c1 + 1));
        if (zone.empty()) continue;

        // PickupDateTime = column 4 (between c3+1 and c4-1)
        if (c4 <= c3 + 1) continue;
        size_t dtStart = c3 + 1;
        size_t dtEnd = c4;

        int hour = -1;
        if (!parseHour(line, dtStart, dtEnd, hour)) continue;

        // Update totals
        zoneTrips[zone]++;

        auto it = zoneHourTrips.find(zone);
        if (it == zoneHourTrips.end()) {
            std::array<long long, 24> arr;
            arr.fill(0);
            arr[hour] = 1;
            zoneHourTrips.emplace(zone, arr);
        } else {
            it->second[hour]++;
        }
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, long long>> ZONE;
    auto itObj = ZONE.find(this);
    if (itObj == ZONE.end() || k <= 0) return {};

    const auto& zoneTrips = itObj->second;

    std::vector<ZoneCount> v;
    v.reserve(zoneTrips.size());
    for (const auto& kv : zoneTrips) v.push_back({kv.first, kv.second});

    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count; // desc
        return a.zone < b.zone;                           // asc
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
    static std::unordered_map<const TripAnalyzer*, std::unordered_map<std::string, std::array<long long, 24>>> ZONEH;
    auto itObj = ZONEH.find(this);
    if (itObj == ZONEH.end() || k <= 0) return {};

    const auto& zoneHourTrips = itObj->second;

    std::vector<SlotCount> v;
    v.reserve(zoneHourTrips.size() * 2);

    for (const auto& kv : zoneHourTrips) {
        const std::string& zone = kv.first;
        const auto& arr = kv.second;
        for (int h = 0; h < 24; ++h) {
            long long cnt = arr[h];
            if (cnt > 0) v.push_back({zone, h, cnt});
        }
    }

    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count; // desc
        if (a.zone != b.zone) return a.zone < b.zone;     // asc
        return a.hour < b.hour;                           // asc
    };

    if ((int)v.size() > k) {
        std::partial_sort(v.begin(), v.begin() + k, v.end(), cmp);
        v.resize(k);
    } else {
        std::sort(v.begin(), v.end(), cmp);
    }
    return v;
}
