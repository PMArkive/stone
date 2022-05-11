// vim: set sts=4 ts=8 sw=4 tw=99 et:
//
// Copyright (C) 2016-2020 David Anderson
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "datasource-wikipedia.h"

#include <google/protobuf/text_format.h>

#include "campaign.h"
#include "context.h"
#include "logging.h"
#include "utility.h"

namespace stone {

bool
FetchHouse(Context* cx, Campaign* cc, const std::string& url, HouseRatingMap* map)
{
    auto data = cx->Download(url, true);
    if (data.empty()) {
        Err() << "Could not download house ratings from Wikipedia";
        return false;
    }

    std::string output;
    std::vector<std::string> argv = {
        GetExecutableDir() + "/scrape-wikipedia-house",
    };
    if (!Run(argv, data, &output))
        return false;

    HouseRatingList msg;
    if (!google::protobuf::TextFormat::ParseFromString(output, &msg)) {
        Err() << "Could not parse HouseData text proto";
        return false;
    }

    auto& districts = cc->district_to_house_race();
    for (auto& hr : *msg.mutable_ratings()) {
        auto iter = districts.find(hr.district());
        if (iter == districts.end()) {
            Err() << "WARNING: No race found for district " << hr.district();
            continue;
        }

        HouseRating new_hr = hr;
        new_hr.clear_district();
        new_hr.set_race_id(iter->second);
        (*map)[iter->second] = std::move(new_hr);
    }
    return true;
}

bool
DataSourceWikipedia::FetchHouseRatings(Context* cx, Campaign* cc, int year, HouseRatingMap* map)
{
    std::string url;
    switch (year) {
        case 2022:
            url = "https://en.wikipedia.org/wiki/2022_United_States_House_of_Representatives_election_ratings";
            break;
        case 2020:
            url = "https://en.wikipedia.org/wiki/2020_United_States_House_of_Representatives_election_ratings";
            break;
        case 2018:
            url = "https://en.wikipedia.org/wiki/2018_United_States_House_of_Representatives_election_ratings";
            break;
        default:
            Err() << "DataSourceWikipedia: unhandled year: " << year;
            return false;
    }
    return FetchHouse(cx, cc, url, map);
}

} // namespace stone

