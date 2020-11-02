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
#include "campaign.h"

#include <amtl/am-string.h>
#include <google/protobuf/text_format.h>

#include "datasource-538.h"
#include "datasource-wikipedia.h"
#include "datasource-rcp.h"
#include "ini-reader.h"
#include "logging.h"
#include "utility.h"

namespace stone {

const std::unordered_map<std::string, std::string> kStateCodes = {
    {"Alabama", "AL"},
    {"Alaska", "AK"},
    {"Arizona", "AZ"},
    {"Arkansas", "AR"},
    {"California", "CA"},
    {"Colorado", "CO"},
    {"Connecticut", "CT"},
    {"Delaware", "DE"},
    {"District of Columbia", "DC"},
    {"Florida", "FL"},
    {"Georgia", "GA"},
    {"Hawaii", "HI"},
    {"Idaho", "ID"},
    {"Illinois", "IL"},
    {"Indiana", "IN"},
    {"Iowa", "IA"},
    {"Kansas", "KS"},
    {"Kentucky", "KY"},
    {"Louisiana", "LA"},
    {"Maine", "ME"},
    {"Maine CD-1", "ME1"},
    {"Maine CD-2", "ME2"},
    {"Maryland", "MD"},
    {"Massachusetts", "MA"},
    {"Michigan", "MI"},
    {"Minnesota", "MN"},
    {"Mississippi", "MS"},
    {"Missouri", "MO"},
    {"Montana", "MT"},
    {"Nebraska", "NE"},
    {"Nebraska CD-1", "NE1"},
    {"Nebraska CD-2", "NE2"},
    {"Nebraska CD-3", "NE3"},
    {"Nevada", "NV"},
    {"New Hampshire", "NH"},
    {"New Jersey", "NJ"},
    {"New Mexico", "NM"},
    {"New York", "NY"},
    {"North Carolina", "NC"},
    {"North Dakota", "ND"},
    {"Ohio", "OH"},
    {"Oklahoma", "OK"},
    {"Oregon", "OR"},
    {"Pennsylvania", "PA"},
    {"Rhode Island", "RI"},
    {"South Carolina", "SC"},
    {"South Dakota", "SD"},
    {"Tennessee", "TN"},
    {"Texas", "TX"},
    {"Utah", "UT"},
    {"Vermont", "VT"},
    {"Virginia", "VA"},
    {"Washington", "WA"},
    {"West Virginia", "WV"},
    {"Wisconsin", "WI"},
    {"Wyoming", "WY"},
};

std::optional<Feed>
Campaign::Fetch(Context* cx)
{
    std::optional<Feed> feed;

    if (default_feed_ == "fivethirtyeight") {
        switch (end_date_.year()) {
            case 2020:
                feed = DataSource538::Fetch2020(cx, this);
                break;
            case 2018:
                feed = DataSource538::Fetch2018(cx, this);
                break;
            case 2016:
                feed = DataSource538::Fetch2016(cx, senate_map_);
                break;
            default:
                Err() << "No 538 feeds found";
                break;
        }
    } else if (default_feed_ == "rcp") {
        feed = DataSourceRcp::Fetch(cx, this);
    } else {
        Err() << "Unknown feed type: " << default_feed_;
        return {};
    }
    if (!feed)
        return {};

    if (governor_feed_ == "rcp") {
        auto gov_polls = DataSourceRcp::FetchGovernors(cx, this, end_date_.year());
        if (!gov_polls) {
            Err() << "No RCP governor feed";
            return {};
        }
        *feed->mutable_governor_polls() = std::move(gov_polls.value());
    } else if (!governor_feed_.empty()) {
        Err() << "Unknown governor feed type: " << governor_feed_;
    }

    if (house_ratings_feed_ == "wikipedia") {
        if (!DataSourceWikipedia::FetchHouseRatings(cx, this, end_date_.year(),
                                                    feed->mutable_house_ratings()))
        {
            return {};
        }
    } else if (!house_ratings_feed_.empty()) {
        Err() << "Unknown house ratings feed type: " << house_ratings_feed_;
    }

    if (generic_ballot_feed_ == "rcp") {
        auto polls = DataSourceRcp::FetchGenericBallot(cx, this, end_date_.year());
        if (!polls) {
            Err() << "No RCP generic ballot polls found";
            return {};
        }
        *feed->mutable_generic_ballot_polls() = std::move(polls.value());
    } else if (!generic_ballot_feed_.empty()) {
        Err() << "Unknown generic ballot feed type: " << generic_ballot_feed_;
    }

    return feed;
}

bool
Campaign::Init(Context* cx, int year)
{
    std::string year_s = std::to_string(year);
    auto dir = GetExecutableDir() + "/data/" + year_s;

    IniFile main;
    auto main_file = dir + "/election-" + year_s + ".ini";
    if (!ParseIni(main_file, &main))
        return false;
    if (!InitMain(main, main_file))
        return false;
    if (!InitAssumedMargins(main, main_file))
        return false;
    if (!InitSenate(main, main_file))
        return false;
    if (!InitGovernor(main, main_file))
        return false;
    if (!InitHouse(main, main_file))
        return false;
    if (!InitImportantDates(main, main_file))
        return false;

    auto siter = main.find("feeds");
    if (siter == main.end()) {
        Err() << "Could not find feeds in " << main_file;
        return false;
    }
    const auto& feed_section = siter->second;

    auto iter = feed_section.find("default");
    if (iter == feed_section.end()) {
        Err() << "No default feed found in " << main_file;
        return false;
    }
    default_feed_ = iter->second;

    if (iter = feed_section.find("house_ratings"); iter != feed_section.end())
        house_ratings_feed_ = iter->second;
    if (iter = feed_section.find("governors"); iter != feed_section.end())
        governor_feed_ = iter->second;
    if (iter = feed_section.find("generic_ballot"); iter != feed_section.end())
        generic_ballot_feed_ = iter->second;

    InitBannedPolls(main);

    auto saved_ratings = dir + "/saved-house-ratings.proto.text";
    if (FileExists(saved_ratings)) {
        std::string data;
        if (!ReadFile(saved_ratings, &data))
            return false;

        if (!google::protobuf::TextFormat::ParseFromString(data, &house_history_))
            return false;
    }

    for (const auto& race : house_map_.races()) {
        size_t index = district_to_house_race_.size();
        district_to_house_race_[race.region()] = index;
    }

    auto results_file = dir + "/results-" + year_s + ".ini";
    if (FileExists(results_file)) {
        if (!InitElectionResults(results_file))
            return false;
    }

    return true;
}

bool
Campaign::InitMain(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("campaign");
    if (siter == file.end()) {
        Err() << "No campaign section found in " << file_name;
        return false;
    }
    const auto& section = siter->second;

    auto iter = section.find("start_date");
    if (iter == section.end()) {
        Err() << "No start_date in " << file_name;
        return false;
    }
    if (!ParseYyyyMmDd(iter->second, &start_date_)) {
        Err() << "Invalid start_date in " << file_name;
        return false;
    }

    iter = section.find("end_date");
    if (iter == section.end()) {
        Err() << "No end_date in " << file_name;
        return false;
    }
    if (!ParseYyyyMmDd(iter->second, &end_date_)) {
        Err() << "Invalid end_date in " << file_name;
        return false;
    }

    iter = section.find("type");
    if (iter == section.end()) {
        Err() << "No campaign type found in " << file_name;
        return false;
    }
    if (iter->second == "president") {
        is_presidential_year_ = true;
    } else if (iter->second != "midyear") {
        Err() << "Unknown campaign type in " << file_name;
        return false;
    }

    iter = section.find("undecideds");
    if (iter == section.end()) {
        Err() << "No campaign undecideds found in " << file_name;
        return false;
    }
    if (!ParseFloat(iter->second, &undecided_percent_)) {
        Err() << "Invalid undecided number in " << file_name;
        return false;
    }

    if (iter = section.find("dem"); iter != section.end())
        dem_pres_ = iter->second;
    if (iter = section.find("gop"); iter != section.end())
        gop_pres_ = iter->second;

    iter = section.find("state_map");
    if (iter == section.end()) {
        Err() << "No state_map found in " << file_name;
        return false;
    }
    if (!InitStateMap(iter->second))
        return false;

    return true;
}

std::optional<std::pair<double, double>>
ParseMargins(const std::string& text, std::string_view file_name)
{
    auto parts = ke::Split(text, " - ");

    if (parts.size() != 2) {
        Err() << "Invalid margin " << text << " in " << file_name;
        return {};
    }

    std::pair<double, double> margins;
    if (!ParseFloat(parts[0], &margins.first) || !ParseFloat(parts[1], &margins.second)) {
        Err() << "Invalid margin " << text << " in " << file_name;
        return {};
    }
    return {margins};
}

bool
Campaign::InitAssumedMargins(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("assumed_margins");
    if (siter == file.end())
        return true;
    const auto& section = siter->second;

    for (const auto& [name, value] : section) {
        auto m = ParseMargins(value, file_name);
        if (!m)
            return false;
        assumed_margins_[name] = *m;
    }
    return true;
}

bool
Campaign::InitImportantDates(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("important_dates");
    if (siter == file.end())
        return true;
    const auto& section = siter->second;

    for (const auto& [key, value] : section) {
        auto parts = ke::Split(key, "-");
        if (parts.size() != 2) {
            Err() << "Invalid date " << key << " in " << file_name;
            return false;
        }

        int day, month;
        if (!ParseInt(parts[0], &month)) {
            Err() << "Invalid month " << parts[0] << " in " << file_name;
            return false;
        }
        if (!ParseInt(parts[1], &day)) {
            Err() << "Invalid month " << parts[1] << " in " << file_name;
            return false;
        }

        ImportantDate idate;
        idate.mutable_date()->set_year(end_date_.year());
        idate.mutable_date()->set_month(month);
        idate.mutable_date()->set_day(day);
        idate.set_label(value);
        important_dates_.emplace_back(std::move(idate));
    }

    auto cmp = [](const ImportantDate& a, const ImportantDate& b) -> bool {
        return a.date() < b.date();
    };
    std::sort(important_dates_.begin(), important_dates_.end(), cmp);
    return true;
}

bool
Campaign::InitSenate(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("senate");
    if (siter == file.end())
        return true;
    const auto& section = siter->second;

    int v;

    auto iter = section.find("dem_seats");
    if (iter == section.end()) {
        Err() << "Senate dem_seats not found in " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, &v)) {
        Err() << "Senate dem_seats value invalid in " << file_name;
        return false;
    }
    senate_map_.mutable_seats()->set_dem(v);

    iter = section.find("gop_seats");
    if (iter == section.end()) {
        Err() << "Senate gop_seats not found in " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, &v)) {
        Err() << "Senate gop_seats value invalid in " << file_name;
        return false;
    }
    senate_map_.mutable_seats()->set_gop(v);

    senate_map_.set_total_seats(senate_map_.seats().dem() + senate_map_.seats().gop());

    iter = section.find("dem_seats_for_control");
    if (iter == section.end()) {
        Err() << "Senate dem_seats_for_control not found in " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, &v)) {
        Err() << "Senate dem_seats_for_control value invalid in " << file_name;
        return false;
    }
    senate_map_.set_dem_seats_for_control(v);

    std::string year_s = std::to_string(end_date_.year());
    auto file_path = GetExecutableDir() + "/data/" + year_s + "/senate-" + year_s + ".ini";

    if (!InitRaceList(file_path, Race::SENATE, senate_map_.mutable_races(),
                      senate_map_.mutable_seats_up()))
    {
        return false;
    }
    return true;
}

static bool
GetIntValue(const IniSection& section, std::string_view section_name, const std::string& key,
            std::string_view file_name, int* v)
{
    auto iter = section.find(key);
    if (iter == section.end()) {
        Err() << key << "not found in section " << section_name << " of " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, v)) {
        Err() << key << "invalid value in section " << section_name << " of " << file_name;
        return false;
    }
    return true;
}

bool
Campaign::InitHouse(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("house");
    if (siter == file.end())
        return true;
    const auto& section = siter->second;

    int v;

    if (!GetIntValue(section, "house", "dem_seats", file_name, &v))
        return false;
    house_map_.mutable_seats()->set_dem(v);
    if (!GetIntValue(section, "house", "gop_seats", file_name, &v))
        return false;
    house_map_.mutable_seats()->set_gop(v);
    house_map_.set_total_seats(house_map_.seats().dem() + house_map_.seats().gop());

    std::string year_s = std::to_string(end_date_.year());
    auto file_path = GetExecutableDir() + "/data/" + year_s + "/house-" + year_s + ".ini";

    if (!InitRaceList(file_path, Race::HOUSE, house_map_.mutable_races(), nullptr))
        return false;
    return true;
}

bool
Campaign::InitGovernor(const IniFile& file, std::string_view file_name)
{
    auto siter = file.find("governors");
    if (siter == file.end())
        return true;
    const auto& section = siter->second;

    int v;

    auto iter = section.find("dem_seats");
    if (iter == section.end()) {
        Err() << "Governor dem_seats not found in " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, &v)) {
        Err() << "Governor dem_seats value invalid in " << file_name;
        return false;
    }
    governor_map_.mutable_seats()->set_dem(v);

    iter = section.find("gop_seats");
    if (iter == section.end()) {
        Err() << "Governor gop_seats not found in " << file_name;
        return false;
    }
    if (!ParseInt(iter->second, &v)) {
        Err() << "Governor gop_seats value invalid in " << file_name;
        return false;
    }
    governor_map_.mutable_seats()->set_gop(v);

    std::string year_s = std::to_string(end_date_.year());
    auto file_path = GetExecutableDir() + "/data/" + year_s + "/governors-" + year_s + ".ini";

    if (!InitRaceList(file_path, Race::GOVERNOR, governor_map_.mutable_races(),
                      governor_map_.mutable_seats_up()))
    {
        return false;
    }
    return true;
}

static Candidate
NameToCandidate(const std::string& name, const std::string& party)
{
    Candidate c;
    c.set_caucus(party);
    if (auto sep = name.find(':'); sep != std::string::npos) {
        c.set_name(name.substr(sep + 1));
        c.set_party(name.substr(0, sep));
    } else {
        c.set_name(name);
    }
    return c;
}

bool
Campaign::InitRaceList(const std::string& file_path, Race_RaceType race_type,
                       ::google::protobuf::RepeatedPtrField<Race>* races, MapEv* seats_up)
{
    OrderedIniFile file;
    if (!ParseIni(file_path, &file))
        return false;

    for (const auto& [region, kv] : file) {
        Race r;
        r.set_region(region);
        r.set_type(race_type);
        r.set_race_id((int)races->size());

        auto iter = kv.find("current_holder");
        if (iter != kv.end())
            r.set_current_holder(iter->second);

        if (seats_up) {
            if (r.current_holder() == "dem")
                seats_up->set_dem(seats_up->dem() + 1);
            else if (r.current_holder() == "gop")
                seats_up->set_gop(seats_up->gop() + 1);
            else
                Err() << "Warning: unattributed seat for " << region << " in " << file_path;
        }

        iter = kv.find("presumed_winner");
        if (iter != kv.end())
            r.set_presumed_winner(iter->second);

        iter = kv.find("dem");
        if (iter != kv.end())
            *(r.mutable_dem()) = NameToCandidate(iter->second, "dem");

        iter = kv.find("gop");
        if (iter != kv.end())
            *(r.mutable_gop()) = NameToCandidate(iter->second, "gop");

        iter = kv.find("rating");
        if (iter != kv.end())
            *(r.mutable_rating()) = iter->second;

        *races->Add() = std::move(r);
    }
    return true;
}

bool
Campaign::InitStateMap(const std::string& name)
{
    auto file_name = GetExecutableDir() + "/data/state-map-" + name + ".ini";

    OrderedIniFile main;
    if (!ParseIni(file_name, &main))
        return false;

    for (const auto& [name, section] : main) {
        auto iter = section.find("evs");
        if (iter == section.end()) {
            Err() << "Could not find evs for state " << name << " in " << file_name;
            return false;
        }

        int evs;
        if (!ParseInt(iter->second, &evs)) {
            Err() << "Invalid ev value for state " << name << " in " << file_name;
            return false;
        }
        total_evs_ += evs;

        State state;
        state.set_name(name);
        state.set_evs(evs);
        state.set_race_id((int)state_list_.size());
        if (auto iter = kStateCodes.find(name); iter != kStateCodes.end())
            state.set_code(iter->second);
        if (auto pos = name.find('-'); pos != std::string::npos) {
            auto parent = name.substr(0, name.rfind(' '));
            state.set_parent(parent);
        }

        state_list_.emplace_back(state);
        states_[name] = std::move(state);
    }
    return true;
}

void
Campaign::InitBannedPolls(const IniFile& file)
{
    auto siter = file.find("banned_polls");
    if (siter == file.end())
        return;
    const auto& section = siter->second;

    for (const auto& [key, _] : section)
        banned_polls_.emplace(key);
}

template <typename T>
static const Race*
FindRace(const T& map, const std::string& region)
{
    for (const auto& iter : map.races()) {
        if (iter.region() == region)
            return &iter;
    }
    return nullptr;
}

bool
Campaign::InitElectionResults(std::string_view file_name)
{
    IniFile file;
    if (!ParseIni(file_name, &file))
        return false;

    if (auto siter = file.find("president"); siter != file.end()) {
        const auto& section = siter->second;

        RaceResultMap map;
        for (const auto& [state_name, margin_data] : section) {
            auto iter = states_.find(state_name);
            if (iter == states_.end()) {
                Err() << "Invalid state name: " << state_name;
                return false;
            }
            auto m = ParseMargins(margin_data, file_name);
            if (!m)
                return false;
            map[iter->second.race_id()] = *m;
        }
        race_results_.emplace(Race::ELECTORAL_COLLEGE, std::move(map));
    }

    if (auto siter = file.find("senate"); siter != file.end()) {
        const auto& section = siter->second;

        RaceResultMap map;
        for (const auto& [region_name, margin_data] : section) {
            const Race* race = FindRace(senate_map_, region_name);
            if (!race) {
                Err() << "Invalid senate seat: " << region_name;
                return false;
            }
            auto m = ParseMargins(margin_data, file_name);
            if (!m)
                return false;
            map[race->race_id()] = *m;
        }
        race_results_.emplace(Race::SENATE, std::move(map));
    }

    if (auto siter = file.find("house"); siter != file.end()) {
        const auto& section = siter->second;

        RaceResultMap map;
        for (const auto& [region_name, margin_data] : section) {
            const Race* race = FindRace(house_map_, region_name);
            if (!race) {
                Out() << "Ignoring unlisted house seat: " << region_name;
                continue;
            }
            auto m = ParseMargins(margin_data, file_name);
            if (!m)
                return false;
            map[race->race_id()] = *m;
        }
        race_results_.emplace(Race::HOUSE, std::move(map));
    }

    if (auto siter = file.find("governors"); siter != file.end()) {
        const auto& section = siter->second;

        RaceResultMap map;
        for (const auto& [region_name, margin_data] : section) {
            const Race* race = FindRace(governor_map_, region_name);
            if (!race) {
                Err() << "Invalid governor seat: " << region_name;
                return false;
            }
            auto m = ParseMargins(margin_data, file_name);
            if (!m)
                return false;
            map[race->race_id()] = *m;
        }
        race_results_.emplace(Race::GOVERNOR, std::move(map));
    }

    if (auto siter = file.find("other"); siter != file.end()) {
        const auto& section = siter->second;
        if (auto iter = section.find("national"); iter != section.end()) {
            auto m = ParseMargins(iter->second, file_name);
            if (!m)
                return false;
            national_race_results_[Race::NATIONAL] = *m;
        }
        if (auto iter = section.find("evs"); iter != section.end()) {
            auto m = ParseMargins(iter->second, file_name);
            if (!m)
                return false;
            national_race_results_[Race::ELECTORAL_COLLEGE] = *m;
        }
        if (auto iter = section.find("generic-ballot"); iter != section.end()) {
            auto m = ParseMargins(iter->second, file_name);
            if (!m)
                return false;
            national_race_results_[Race::GENERIC_BALLOT] = *m;
        }
    }

    return true;
}

} // namespace stone
