// vim: set sts=4 ts=8 sw=4 tw=99 et:
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <amtl/am-string.h>
#include <date/date.h>
#include <google/protobuf/text_format.h>
#include <proto/history.pb.h>

using namespace std::string_literals;
using namespace stone;

static std::pair<std::string, std::string>
GetRatingTuple(const RaceModel& rm)
{
    if (rm.win_prob() > 0.5)
        return std::make_pair("dem"s, rm.rating());
    if (rm.win_prob() < 0.5)
        return std::make_pair("gop"s, rm.rating());
    return std::make_pair("", rm.rating());
}

int main(int argc, char** argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (argc < 2) {
        std::cerr << "Must specify a campaign.\n";
        return 1;
    }

    std::string path = argv[1] + "/history.bin"s;
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

    HouseRatingHistory history;
    std::map<int32_t, std::pair<std::string, std::string>> prev_ratings;
    std::map<int32_t, std::pair<std::string, std::string>> ratings;

    for (auto iter = data.history().rbegin(); iter != data.history().rend(); iter++) {
        const auto& model = *iter;

        ratings.clear();
        for (const auto& rm : model.house_races()) {
            if (rm.rating().empty())
                continue;
            ratings[rm.race_id()] = GetRatingTuple(rm);
        }

        // Check for anything added or changed.
        bool changed = false;
        for (const auto& [race_id, rating_tuple] : ratings) {
            auto p = prev_ratings.find(race_id);
            if (p == prev_ratings.end() || p->second != rating_tuple) {
                changed = true;
                break;
            }
        }

        // Check for anything removed.
        if (!changed) {
            for (const auto& [race_id, rating_tuple] : prev_ratings) {
                auto p = ratings.find(race_id);
                if (p == ratings.end()) {
                    changed = true;
                    break;
                }
            }
        }

        if (!changed)
            continue;

        DatedHouseRatings entry;
        *entry.mutable_date() = model.date();

        for (const auto& [race_id, rating_tuple] : ratings) {
            HouseRating hr;
            hr.set_race_id(race_id);
            hr.set_rating(ke::Split(rating_tuple.second, " ")[0]);
            hr.set_presumed_winner(rating_tuple.first);
            (*entry.mutable_ratings())[race_id] = std::move(hr);
        }
        *history.mutable_entries()->Add() = std::move(entry);

        prev_ratings = std::move(ratings);
    }

    std::string str;
    google::protobuf::TextFormat::PrintToString(history, &str);
    std::cout << str << std::endl;

    return 0;
}
