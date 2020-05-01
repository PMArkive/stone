// vim: set sts=4 ts=8 sw=4 tw=99 et:
#include <math.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <amtl/am-string.h>
#include <date/date.h>
#include <proto/history.pb.h>

using namespace std::string_literals;
using namespace stone;

// :TODO: library this
bool DaysBetween(const Date& first, const Date& second, int* diff);

int main(int argc, char** argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    std::vector<CampaignData> campaigns;
    for (int i = 1; i < argc; i++) {
        std::string path = argv[i] + "/history.bin"s;
        std::fstream input(path, std::ios::in | std::ios::binary);
        if (!input) {
            std::cerr << "error opening: " << path << "\n";
            return 1;
        }

        CampaignData data;
        if (!data.ParseFromIstream(&input)) {
            std::cerr << "error parsing: " << path << "\n";
            return 1;
        }
        campaigns.emplace_back(std::move(data));
    }
    if (campaigns.empty()) {
        std::cerr << "Must specify at least one campaign.\n";
        return 1;
    }

    std::vector<std::string> cols = { "Days Left" };
    for (const auto& data : campaigns)
        cols.emplace_back(data.dem_pres() + " " + std::to_string(data.election_day().year()));
    std::cout << ke::Join(cols, ",") << "\n";

    using MarginHistory = std::vector<std::pair<int, double>>;

    std::vector<MarginHistory> margin_histories;
    for (const auto& data : campaigns) {
        MarginHistory margin_history;
        double low = 1000.0f;
        double high = -1000.0f;
        for (const auto& model : data.history()) {
            int days;
            if (!DaysBetween(model.date(), data.election_day(), &days)) {
                std::cerr << "time error\n";
                return 1;
            }
            if (days < 0)
                continue;
            double value = model.generic_ballot().margin();
            if (value < low)
                low = value;
            if (value > high)
                high = value;
            margin_history.emplace_back(days, fabs(high - low));
        }
        margin_histories.emplace_back(std::move(margin_history));
    }

    std::vector<std::pair<MarginHistory*, MarginHistory::iterator>> iters;
    for (auto& mh : margin_histories)
        iters.emplace_back(&mh, mh.begin());

    int days_left = 0;
    bool done = false;
    std::vector<std::string> lines;
    while (true) {
        double high = std::numeric_limits<double>::min();
        for (auto& pair : iters) {
            if (pair.second == pair.first->end()) {
                // This campaign has no more history, stop.
                done = true;
                break;
            }
            if (pair.second->first != days_left) {
                // This campaign hasn't finished yet.
                assert(pair.second->first > days_left);
                continue;
            }
            high = std::max(high, pair.second->second);
            pair.second++;
        }
        if (done)
            break;
        lines.emplace_back(std::to_string(int(high * 100) / 100.0f));
        days_left++;
    }
    std::cout << ke::Join(lines, ", ") << "\n";

    return 0;
}

bool DaysBetween(const Date& first, const Date& second, int* diff)
{
    struct tm tm_a = { 0, 0, 0, first.day(), first.month() - 1, first.year() - 1900 };
    struct tm tm_b = { 0, 0, 0, second.day(), second.month() - 1, second.year() - 1900 };
    time_t time_a = mktime(&tm_a);
    if (time_a == (time_t)-1)
        return false;
    time_t time_b = mktime(&tm_b);
    if (time_b == (time_t)-1)
        return false;
    *diff = difftime(time_b, time_a) / (60 * 60 * 24);
    return true;
}
