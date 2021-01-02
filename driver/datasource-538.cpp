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

#include <iostream>
#include <string>
#include <optional>

#include <rapidjson/document.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>
#include <csv.hpp>
#include "campaign.h"
#include "context.h"
#include "datasource-538.h"
#include "logging.h"
#include "utility.h"

namespace stone {

using namespace std::string_literals;

typedef rapidjson::GenericValue<rapidjson::UTF8<>> JsonValue;
typedef rapidjson::GenericObject<true, rapidjson::GenericValue<rapidjson::UTF8<char>>> JsonObject;

static bool
Extract2020Choices(const JsonObject& obj, const JsonValue** dem, const JsonValue** gop)
{
    *dem = nullptr;
    *gop = nullptr;

    std::string_view key;
    if (obj["type"].GetString() == "generic-ballot"s)
        key = "choice";
    else
        key = "party";

    const auto& qs = obj["answers"].GetArray();
    for (size_t j = 0; j < qs.Size(); j++) {
        const auto& q = qs[j];
        if (q[key.data()].GetString() == "Dem"s) {
            if (*dem)
                return false;
            *dem = &q;
        } else if (q[key.data()].GetString() == "Rep"s) {
            if (*gop)
                return false;
            *gop = &q;
        }
    }
    return *dem && *gop;
}

static int32_t GetGradeValue(const JsonObject& raw) {
    if (!raw.HasMember("grade"))
        return -1;

    std::string s = raw["grade"].GetString();
    if (s.empty())
        return -1;

    int grade = 0;
    switch (s[0]) {
        case 'A':
            grade = 10;
            break;
        case 'B':
            grade = 7;
            break;
        case 'C':
            grade = 4;
            break;
        case 'D':
            grade = 1;
            break;
        default:
            return -1;
    }
    if (s.size() > 1) {
        if (s[1] == '+')
            grade++;
        else if (s[1] == '-')
            grade--;
    }
    return grade;
}

static Poll
FillPollData(Campaign* cc, const JsonObject& raw, const JsonValue* dem, const JsonValue* gop)
{
    double dem_pct, gop_pct;
    if (!ParseFloat((*dem)["pct"].GetString(), &dem_pct) ||
        !ParseFloat((*gop)["pct"].GetString(), &gop_pct))
    {
        Err() << "WARNING: bad margin value";
        return {};
    }

    Poll poll;
    poll.set_description(raw["pollster"].GetString());
    poll.set_dem(dem_pct);
    poll.set_gop(gop_pct);
    poll.set_margin(dem_pct - gop_pct);
    poll.set_url(raw["url"].GetString());
    poll.set_id(raw["id"].GetString());
    poll.set_grade(GetGradeValue(raw));
    poll.set_partisan(raw.HasMember("partisan"));

    if (cc->IsPollBanned(poll.id()))
        return {};

    if (!raw["sampleSize"].IsNull()) {
        int samplesize = 0;
        std::string samplesize_str = raw["sampleSize"].GetString();
        if (!samplesize_str.empty() && !ParseInt(samplesize_str, &samplesize)) {
            Err() << "WARNING: bad sample size value: " << samplesize_str;
        } else {
            poll.set_sample_size(samplesize);
        }
    }

    if (!raw["population"].IsNull()) {
        std::string type = raw["population"].GetString();
        poll.set_sample_type(type);
    }

    if (!ParseYyyyMmDd(raw["startDate"].GetString(), poll.mutable_start()) ||
        !ParseYyyyMmDd(raw["endDate"].GetString(), poll.mutable_end()) ||
        !ParseYyyyMmDd(raw["created_at"].GetString(), poll.mutable_published()))
    {
        Err() << "WARNING: bad date value";
        return {};
    }
    return poll;
}

static std::optional<Poll>
ExtractPresidentPoll2020(Campaign* cc, const JsonObject& raw, std::string_view dem_name,
                         std::string_view gop_name)
{
    const JsonValue* dem;
    const JsonValue* gop;
    if (!Extract2020Choices(raw, &dem, &gop))
        return {};

    if ((*gop)["choice"].GetString() != gop_name || (*dem)["choice"].GetString() != dem_name)
        return {};

    Poll poll = FillPollData(cc, raw, dem, gop);
    return std::make_optional<Poll>(std::move(poll));
}

static std::optional<Poll>
ExtractGenericPoll2020(Campaign* cc, const JsonObject& raw, std::string* dem_candidate,
                      std::string* gop_candidate)
{
    const JsonValue* dem;
    const JsonValue* gop;
    if (!Extract2020Choices(raw, &dem, &gop))
        return {};

    *dem_candidate = (*dem)["choice"].GetString();
    *gop_candidate = (*gop)["choice"].GetString();

    Poll poll = FillPollData(cc, raw, dem, gop);
    return std::make_optional<Poll>(std::move(poll));
}

std::optional<Feed>
FetchPollsV2(Context* cx, Campaign* cc, std::string_view dem = {}, std::string_view gop = {})
{
    auto data = cx->Download("https://projects.fivethirtyeight.com/polls/polls.json");
    if (data.empty())
        return {};

    rapidjson::Document src_doc;
    src_doc.Parse(data.c_str());

    const auto& senate_map = cc->senate_map();
    const auto& governor_map = cc->governor_map();

    // Work around type idiocy in rapidjson.
    const rapidjson::Document& doc = src_doc;

    Feed feed;
    feed.mutable_info()->set_description("538 poll feed");
    feed.mutable_info()->set_short_name("default");
    feed.mutable_info()->set_feed_type("normal");

    auto& states = *feed.mutable_states();

    std::unordered_map<std::string, size_t> senate_map_index_map;
    for (const auto& seat : senate_map.races()) {
        // Ignore jungle races since we don't model those yet.
        if (seat.is_jungle())
            continue;
        auto key = seat.dem().name() + "/" + seat.gop().name();
        senate_map_index_map[key] = seat.race_id();
    }

    std::unordered_map<std::string, int32_t> governor_map_index_map;
    for (const auto& seat : governor_map.races()) {
        auto key = seat.dem().name() + "/" + seat.gop().name();
        governor_map_index_map[key] = seat.race_id();
    }

    std::unordered_set<std::string> warnings;

    const auto& districts = cc->district_to_house_race();
    auto& senate_polls = *feed.mutable_senate_polls();

    int year = cc->EndDate().year();

    assert(doc.IsArray());
    for (size_t i = 0; i < doc.Size(); i++) {
        const auto& raw = doc[i].GetObject();

        std::string grade = raw.HasMember("grade") ? raw["grade"].GetString() : "";

        // Skip partisan pollsters that do not have grades.
        if (raw.HasMember("partisan") && grade.empty())
            continue;

        // Skip pollsters with a "D" grade.
        if (!grade.empty() && grade[0] == 'D')
            continue;

        if (raw["type"].GetString() == "president-general"s) {
            if (!cc->IsPresidentialYear())
                continue;

            auto poll = ExtractPresidentPoll2020(cc, raw, dem, gop);
            if (!poll)
                continue;

            // 538 doesn't mark these as tracking in their JSON, so fix that up
            // here.
            if (year == 2020 &&
                (strcmp(raw["pollster"].GetString(), "USC Dornsife") == 0 ||
                 strcmp(raw["pollster"].GetString(), "IBD/TIPP") == 0))
            {
                poll->set_tracking(true);
            }

            std::string state_name = raw["state"].GetString();
            if (raw.HasMember("district"))
                state_name += " CD-"s + raw["district"].GetString();

            if (state_name == "National"s)
                *feed.add_national_polls() = std::move(poll.value());
            else if (auto iter = states.find(state_name); iter != states.end())
                *iter->second.add_polls() = std::move(poll.value());
            else
                *states[state_name].mutable_polls()->Add() = std::move(poll.value());
        } else if (raw["type"].GetString() == "generic-ballot"s) {
            if (cc->election_type() == "runoff")
                continue;

            std::string dem, gop;
            auto poll = ExtractGenericPoll2020(cc, raw, &dem, &gop);
            if (!poll)
                continue;

            // 538 doesn't mark these as tracking in their JSON, so fix that up
            // here.
            if (year == 2020 &&
                (strcmp(raw["pollster"].GetString(), "USC Dornsife") == 0))
            {
                poll->set_tracking(true);
            }

            *feed.add_generic_ballot_polls() = std::move(poll.value());
        } else if (raw["type"].GetString() == "senate"s) {
            std::string dem, gop;
            auto poll = ExtractGenericPoll2020(cc, raw, &dem, &gop);
            if (!poll)
                continue;

            auto key = dem + "/" + gop;
            auto iter = senate_map_index_map.find(key);
            if (iter == senate_map_index_map.end())
                continue;

            if (auto ki = senate_polls.find(iter->second); ki != senate_polls.end()) {
                *ki->second.mutable_polls()->Add() = std::move(poll.value());
            } else {
                PollList list;
                *list.mutable_polls()->Add() = std::move(poll.value());
                senate_polls.insert({(int)iter->second, std::move(list)});
            }
        } if (raw["type"].GetString() == "governor"s) {
            std::string dem, gop;
            auto poll = ExtractGenericPoll2020(cc, raw, &dem, &gop);
            if (!poll)
                continue;

            auto key = dem + "/" + gop;
            auto iter = governor_map_index_map.find(key);
            if (iter == governor_map_index_map.end())
                continue;

            auto& gov_polls = *feed.mutable_governor_polls();
            if (auto gi = gov_polls.find(iter->second); gi != gov_polls.end()) {
                *(gi->second.mutable_polls()->Add()) = std::move(poll.value());
            } else {
                PollList list;
                *list.mutable_polls()->Add() = std::move(poll.value());
                gov_polls.insert({(int)iter->second, std::move(list)});
            }
        } else if (raw["type"].GetString() == "house"s) {
            std::string dem, gop;
            auto poll = ExtractGenericPoll2020(cc, raw, &dem, &gop);
            if (!poll)
                continue;

            if (poll->end() < cc->StartDate())
                continue;

            auto district = raw["state"].GetString() + " "s + raw["district"].GetString();
            auto iter = districts.find(district);
            if (iter == districts.end() && raw["district"].GetString() == "1"s) {
                // Look for at-large districts.
                iter = districts.find(raw["state"].GetString());
            }
            if (iter == districts.end())
                continue;

            // Challengers must match unless TBD.
            const auto& race = cc->house_map().races()[iter->second];
            if (race.dem().name() != dem && race.dem().name() != "TBD") {
                if (warnings.count(district) == 0) {
                    Out() << "Warning: skipping poll for " << district << ", dem "
                          << "\"" << dem << "\" does not match \"" << race.dem().name() << "\"";
                }
                warnings.emplace(district);
                continue;
            }
            if (race.gop().name() != gop && race.gop().name() != "TBD") {
                if (warnings.count(district) == 0) {
                    Out() << "Warning: skipping poll for " << district << ", gop "
                          << "\"" << gop << "\" does not match \"" << race.gop().name() << "\"";
                }
                warnings.emplace(district);
                continue;
            }

            auto& list = (*feed.mutable_house_polls())[iter->second];
            *list.add_polls() = std::move(poll.value());
        }
    }

    return {std::move(feed)};
}

static void
Add2016Poll(Feed* feed, const std::string& state, const Poll& poll)
{
    auto& states = *feed->mutable_states();

    Poll* dest = nullptr;
    if (state == "U.S.")
        dest = feed->add_national_polls();
    else if (auto iter = states.find(state); iter != states.end())
        dest = iter->second.add_polls();
    else
        dest = states[state].mutable_polls()->Add();
    *dest = poll;
}

static bool
Extract2016Choices(const JsonObject& obj, const JsonValue** dem, const JsonValue** gop)
{
    *dem = nullptr;
    *gop = nullptr;

    const auto& qs = obj["votingAnswers"].GetArray();
    for (size_t j = 0; j < qs.Size(); j++) {
        const auto& q = qs[j];
        if (q["party"].GetString() == "D"s) {
            if (*dem)
                return false;
            *dem = &q;
        } else if (q["party"].GetString() == "R"s) {
            if (*gop)
                return false;
            *gop = &q;
        }
    }
    return *dem && *gop;
}

std::optional<Feed>
DataSource538::Fetch2016(Context* cx, const SenateMap& senate_map)
{
    auto data = cx->Download("https://projects.fivethirtyeight.com/general-model/president_general_polls_2016.csv");
    if (data.empty())
        return {};

    Feed normal;

    normal.mutable_info()->set_description("538 Poll Feed");
    normal.mutable_info()->set_short_name("default");
    normal.mutable_info()->set_feed_type("normal");

    auto rows = csv::parse(data);
    for (const auto& row : rows) {
        if (row["type"].get<>() != "polls-only")
            continue;

        Poll poll;
        poll.set_description(row["pollster"].get<>());

        // Banned pollsters.
        if (poll.description() == "SurveyMonkey")
            continue;

        if (!ParseMonthDayYear(row["startdate"].get<>(), poll.mutable_start()) ||
            !ParseMonthDayYear(row["enddate"].get<>(), poll.mutable_end()))
        {
            Err() << "WARNING: bad date value";
            continue;
        }
        if (!ParseMonthDayYear(row["createddate"].get<>(), poll.mutable_published())) {
            Err() << "WARNING: bad createddate value";
            continue;
        }

        int samplesize = 0;
        auto samplesize_str = row["samplesize"].get<>();
        if (!samplesize_str.empty() && !ParseInt(samplesize_str, &samplesize)) {
            Err() << "WARNING: bad sample size value: " << row["samplesize"].get<>();
            continue;
        }
        poll.set_sample_size(samplesize);

        auto poll_type = row["population"].get<>();
        poll.set_sample_type(poll_type);

        double dem_pct, gop_pct;
        if (!ParseFloat(row["rawpoll_clinton"].get<>(), &dem_pct) ||
            !ParseFloat(row["rawpoll_trump"].get<>(), &gop_pct))
        {
            Err() << "WARNING: bad percent value";
            continue;
        }
        poll.set_dem(dem_pct);
        poll.set_gop(gop_pct);
        poll.set_margin(dem_pct - gop_pct);
        poll.set_url(row["url"].get<>());
        poll.set_id(row["poll_id"].get<>());
        Add2016Poll(&normal, row["state"].get<>(), poll);
    }

    auto senate_data = cx->Download("https://projects.fivethirtyeight.com/2016-election-forecast/senate/updates.json");
    if (senate_data.empty())
        return {std::move(normal)};

    rapidjson::Document src_doc;
    rapidjson::ParseResult ok = src_doc.Parse(senate_data.c_str());
    if (!ok) {
        Err() << "parse error: " << rapidjson::GetParseError_En(ok.Code()) << " at "
              << ok.Offset();
        return {std::move(normal)};
    }

    // Work around type idiocy in rapidjson.
    const rapidjson::Document& doc = src_doc;
    assert(doc.IsArray());

    std::unordered_map<std::string, size_t> senate_map_index_map;
    for (const auto& seat : senate_map.races()) {
        auto iter = kStateCodes.find(seat.region());
        if (iter == kStateCodes.end()) {
            Err() << "Unknown state: " << seat.region();
            continue;
        }
        senate_map_index_map[iter->second] = seat.race_id();
    }

    auto& senate_polls = *normal.mutable_senate_polls();
    for (size_t i = 0; i < doc.Size(); i++) {
        const auto& raw = doc[i].GetObject();
        const auto& state_name = raw["state"].GetString();

        auto iter = senate_map_index_map.find(state_name);
        if (iter == senate_map_index_map.end())
            continue;

        const JsonValue* dem;
        const JsonValue* gop;
        if (!Extract2016Choices(raw, &dem, &gop))
            continue;

        Poll poll;

        if (!raw["sampleSize"].IsNull()) {
            int samplesize = 0;
            std::string samplesize_str = raw["sampleSize"].GetString();
            if (!samplesize_str.empty() && !ParseInt(samplesize_str, &samplesize)) {
                Err() << "WARNING: bad sample size value: " << samplesize_str;
            } else {
                poll.set_sample_size(samplesize);
            }
        }
        if (!raw["population"].IsNull()) {
            std::string type = raw["population"].GetString();
            poll.set_sample_type(type);
        }

        poll.set_url(raw["url"].GetString());
        poll.set_description(raw["pollster"].GetString());
        poll.set_dem((*dem)["pct"].GetDouble());
        poll.set_gop((*gop)["pct"].GetDouble());
        poll.set_margin(poll.dem() - poll.gop());
        if (!ParseYyyyMmDd(raw["startDate"].GetString(), poll.mutable_start()) ||
            !ParseYyyyMmDd(raw["endDate"].GetString(), poll.mutable_end()))
        {
            Err() << "WARNING: bad date value";
            return {};
        }

        if (auto ki = senate_polls.find(iter->second); ki != senate_polls.end()) {
            *ki->second.mutable_polls()->Add() = std::move(poll);
        } else {
            PollList list;
            *list.mutable_polls()->Add() = std::move(poll);
            senate_polls.insert({(int)iter->second, std::move(list)});
        }
    }

    return {std::move(normal)};
}

std::optional<Feed>
DataSource538::Fetch(Context* cx, Campaign* cc)
{
    switch (cc->EndDate().year()) {
        case 2020:
            return FetchPollsV2(cx, cc, "Biden", "Trump");
        case 2021:
        case 2018:
            return FetchPollsV2(cx, cc);
        case 2016:
            return DataSource538::Fetch2016(cx, cc->senate_map());
        default:
            Err() << "No 538 feeds found";
            return {};
    }
}

} // namespace stone
