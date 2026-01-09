#include <bits/stdc++.h>

using namespace std;
 
struct ZoneCount {

    string zone;

    long long count;

};
 
struct SlotCount {

    string zone;

    int hour;

    long long count;

};
 
class TripAnalyzer {

private:

    unordered_map<string, long long> zoneCount;

    unordered_map<string, unordered_map<int, long long>> zoneHourCount;
 
    bool parseHour(const string& datetime, int& hour) {

        if (datetime.size() < 13) return false;

        try {

            hour = stoi(datetime.substr(11, 2));

            return hour >= 0 && hour <= 23;

        } catch (...) {

            return false;

        }

    }
 
public:

    void ingestStdin() {

        string line;

        if (!getline(cin, line)) return; // empty input
 
        while (getline(cin, line)) {

            stringstream ss(line);

            vector<string> cols;

            string cell;
 
            while (getline(ss, cell, ',')) {

                cols.push_back(cell);

            }
 
            if (cols.size() < 6) continue;

            if (cols[1].empty() || cols[3].empty()) continue;
 
            int hour;

            if (!parseHour(cols[3], hour)) continue;
 
            const string& zone = cols[1];

            zoneCount[zone]++;

            zoneHourCount[zone][hour]++;

        }

    }
 
    vector<ZoneCount> topZones(int k = 10) {

        vector<ZoneCount> v;

        for (auto& p : zoneCount)

            v.push_back({p.first, p.second});
 
        sort(v.begin(), v.end(), [](const ZoneCount& a, const ZoneCount& b) {

            if (a.count != b.count) return a.count > b.count;

            return a.zone < b.zone;

        });
 
        if ((int)v.size() > k) v.resize(k);

        return v;

    }
 
    vector<SlotCount> topBusySlots(int k = 10) {

        vector<SlotCount> v;

        for (auto& z : zoneHourCount) {

            for (auto& h : z.second) {

                v.push_back({z.first, h.first, h.second});

            }

        }
 
        sort(v.begin(), v.end(), [](const SlotCount& a, const SlotCount& b) {

            if (a.count != b.count) return a.count > b.count;

            if (a.zone != b.zone) return a.zone < b.zone;

            return a.hour < b.hour;

        });
 
        if ((int)v.size() > k) v.resize(k);

        return v;

    }

};
 
int main() {

    TripAnalyzer analyzer;

    analyzer.ingestStdin();
 
    cout << "TOP_ZONES\n";

    for (auto& z : analyzer.topZones())

        cout << z.zone << "," << z.count << "\n";
 
    cout << "TOP_SLOTS\n";

    for (auto& s : analyzer.topBusySlots())

        cout << s.zone << "," << s.hour << "," << s.count << "\n";
 
    return 0;

}

 
