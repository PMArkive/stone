// vim: set sts=4 ts=8 sw=4 tw=99 et:
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include <proto/history.pb.h>

using namespace stone;

using ProbMap = std::unordered_map<std::string, std::vector<double>>;

void
AddPrediction(ProbMap* map, const std::string& key, const Prediction& p)
{
    (*map)[key].emplace_back(p.dem_win_p());
}

void
CalcBrier(const ProbMap& map, const std::string& key, double actual_p)
{
    auto iter = map.find(key);
    if (iter == map.end())
        return;
    const auto& vec = iter->second;

    double score = 0.0;
    for (const auto& p : vec)
        score += ((p - actual_p) * (p - actual_p)) / double(vec.size());

    std::cout << "Brier score for " << key << ": " << score << "\n";
}

int main(int argc, char** argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (argc < 2) {
        std::cerr << "Usage: <campaign-history.bin>\n";
        return 1;
    }

    std::fstream input(argv[1], std::ios::in | std::ios::binary);
    if (!input) {
        std::cerr << "error opening: " << argv[1] << "\n";
        return 1;
    }

    CampaignData data;
    if (!data.ParseFromIstream(&input)) {
        std::cerr << "error parsing: " << argv[1] << "\n";
        return 1;
    }

    ProbMap map;
    for (const auto& day : data.history()) {
        if (day.has_ec_prediction())
            AddPrediction(&map, "ec", day.ec_prediction());
        if (day.has_senate_prediction())
            AddPrediction(&map, "senate", day.senate_prediction());
        if (day.has_house_prediction())
            AddPrediction(&map, "house", day.house_prediction());
    }

    CalcBrier(map, "ec", 1.0);
    CalcBrier(map, "senate", 0.0);
    CalcBrier(map, "house", 1.0);

    return 0;
}
