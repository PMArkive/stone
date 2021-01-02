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

#include "htmlgen.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <amtl/am-string.h>
#include <amtl/am-time.h>
#include <amtl/am-vector.h>
#include <amtl/experimental/am-argparser.h>
#include <inja/inja.hpp>
#include "campaign.h"
#include "logging.h"
#include "mathlib.h"
#include "utility.h"

namespace stone {

using namespace std::string_literals;

ke::args::ToggleOption not_backdating(nullptr, "--not-backdating", ke::Some(false), "Override backdating");

Renderer::Renderer(Context* cx, const CampaignData& data)
  : cx_(cx),
    data_(data)
{
    dir_ = cx->GetProp("tpl-dir");
    out_ = cx->GetProp("html-dir");

    for (const auto& state : data_.states()) {
        state_map_.emplace(state.name(), state);
        total_evs_ += state.evs();
    }
    env_ = inja::Environment{dir_ + "/"};
}

void
Renderer::Save(const std::string& file, const std::string& text)
{
    std::string out_path = OutputPath(file);
    if (!SaveFile(text, std::string_view(out_path)))
        abort();
}

std::string
Renderer::OutputPath(const std::string& path)
{
    return out_ + "/" + path;
}

bool
Renderer::OutputExists(const std::string& file)
{
    auto full_path = OutputPath(file);
    return access(full_path.c_str(), R_OK) == 0;
}

bool
Renderer::CalcLatestUpdate(FileTime* time)
{
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(dir_.c_str()), closedir);
    if (!dir) {
        PErr() << "opendir " << dir_ << " failed";
        return false;
    }

    *time = {};

    struct dirent* dt;
    while ((dt = readdir(dir.get())) != nullptr) {
        if (dt->d_name == "."s || dt->d_name == ".."s)
            continue;
        if (!ke::EndsWith(dt->d_name, ".tpl"))
            continue;

        auto path = dir_ + "/" + dt->d_name;

        FileTime tm;
        if (!GetFileModTime(path, &tm))
            return false;
        if (tm > *time)
            *time = tm;
    }
    return true;
}

static bool
MustRegenFile(const std::string& source, const std::string& dest)
{
    if (access(dest.c_str(), F_OK) != 0 && errno == ENOENT)
        return true;

    FileTime src_time, dest_time;
    if (!GetFileModTime(source, &src_time))
        return false;
    if (!GetFileModTime(dest, &dest_time))
        return false;
    return src_time > dest_time;
}

static std::string
DateSuffix(const Date& d)
{
    return std::to_string(d.year()) + "-"s + std::to_string(d.month()) + "-"s +
           std::to_string(d.day());
}

static std::string
SuffixedName(const std::string& name, const Date& d)
{
    std::string prefix = name;
    std::string suffix;
    if (auto pos = prefix.find('.'); pos != std::string::npos) {
        prefix = name.substr(0, pos);
        suffix = name.substr(pos);
    }
    return prefix + "-" + DateSuffix(d) + suffix;
}

bool
Renderer::Generate()
{
    if (dir_.empty()) {
        Err() << "Missing tpl-dir in config.";
        return false;
    }
    if (out_.empty()) {
        Err() << "Missing html-dir in config.";
        return false;
    }
    if (mkdir(out_.c_str(), 0755) && errno != EEXIST) {
        PErr() << "mkdir failed";
        return false;
    }

    if (!CopyNonTemplateFiles())
        return false;

    if (data_.history().empty()) {
        Err() << "No history to generate";
        return false;
    }

    // Determine if we are backdating.
    {
        struct tm tm;
        time_t t = time(nullptr);
        if (!localtime_r(&t, &tm)) {
            PErr() << "Unable to get the current time";
            return false;
        }

        backdating_ = (tm.tm_year + 1900 != data_.election_day().year()) &&
                      (tm.tm_year + 1900 != data_.start_date().year()) &&
                      !not_backdating.value();
    }

    // Check whether we should regenerate everything.
    auto last_gen_time =
        ke::EpochValueToTimePoint<FileTime>(cx_->GetCacheInt64("htmlgen.last_updated", 0));
    FileTime latest_mod;
    if (!CalcLatestUpdate(&latest_mod))
        return false;
    bool regen_all = last_gen_time < latest_mod;

    // Get the latest index file we last wrote.
    std::optional<Date> last_gen_date;
    auto last_date_string = cx_->GetCache("htmlgen.last_date", "");
    if (!last_date_string.empty()) {
        Date date;
        if (ParseYyyyMmDd(last_date_string, &date))
            last_gen_date.emplace(date);
    }
    if (!last_gen_date.has_value())
        regen_all = true;

    bool all_rendered = true;
    const ModelData* prev = nullptr;
    for (auto iter = data_.history().rbegin(); iter != data_.history().rend(); iter++) {
        const auto& model = *iter;

        // Don't regenerate backdated entries unless they're missing.
        auto index_path = SuffixedName("index.html", model.date());

        // Note: always regenerate the last date we generated, since we
        // need to update the "Next" link.
        bool should_regen = false;
        if (regen_all)
            should_regen = true;
        if (!should_regen && !OutputExists(index_path))
            should_regen = true;
        if (!should_regen && last_gen_date && model.date() == *last_gen_date)
            should_regen = true;
        if (!should_regen) {
            auto gen_time_s = std::chrono::seconds{UtcToLocal(model.generated())};
            auto gen_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(gen_time_s);
            FileTime gen_time = FileTime{gen_time_ns};
            FileTime file_time;
            if (GetFileModTime(OutputPath(index_path), &file_time))
                should_regen |= gen_time > file_time;
            else
                should_regen = true;
        }

        if (!should_regen) {
            prev = &model;
            continue;
        }

        const ModelData* this_model = &model;
        auto task = [this, this_model, prev, &all_rendered,
                     index_path{std::move(index_path)}](ThreadPool*) -> void
        {
            HtmlGenerator generator(this, *this_model, prev);
            all_rendered &= generator.RenderMain(index_path);
        };
        cx_->workers().Do(std::move(task));

        prev = &model;
    }

    if (data_.presidential_year()) {
        cx_->workers().Do([this](ThreadPool*) -> void {
            int index = (data_.history()[0].date() <= data_.election_day())
                        ? 0 : 1;
            HtmlGenerator generator(this, data_.history()[index], nullptr);
            generator.RenderWrongometer();
        });
    }

    cx_->workers().Do([this](ThreadPool*) -> void {
        int index = (data_.history()[0].date() <= data_.election_day())
                    ? 0 : 1;
        HtmlGenerator generator(this, data_.history()[index], nullptr);
        generator.RenderVoteShareGraphs();
    });

    cx_->workers().RunCompletionTasks();

    if (!all_rendered)
        return false;

    if (!GenerateGraphs())
        return false;

    // Don't symlink to the final results, force a click-through to a special
    // results page.
    auto symlink_date = std::min(data_.election_day(), data_.history()[0].date());
    auto target_path = SuffixedName("index.html", symlink_date);
    auto link_path = out_ + "/index.html";
    struct stat s;
    if (lstat(link_path.c_str(), &s) == 0 && unlink(link_path.c_str()) < 0) {
        PErr() << "unlink " << link_path;
        return false;
    }
    if (symlink(target_path.c_str(), link_path.c_str()) < 0) {
        PErr() << "symlink " << link_path;
        return false;
    }

    auto now_epoch = std::chrono::system_clock::now().time_since_epoch();
    auto now_epoch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_epoch).count();
    cx_->SetCache("htmlgen.last_updated", std::to_string(now_epoch_ns));
    cx_->SetCache("htmlgen.last_date", DateSuffix(data_.history()[0].date()));

    return true;
}

bool
Renderer::GenerateGraphs()
{
    Out() << "Generating graphs...";

    std::vector<std::string> base_argv = {
        GetExecutableDir() + "/generate-graph",
        cx_->PathTo("history.bin"),
        "batch"s,
    };

    // Divvy up commands between all available threads.
    size_t num_threads = cx_->workers().NumThreads();
    std::vector<std::vector<std::string>> tasks;

    size_t batch = 0;
    while (!graph_commands_.empty()) {
        if (batch >= tasks.size())
            tasks.push_back(base_argv);

        ke::MoveExtend(&tasks[batch], &graph_commands_.back());
        graph_commands_.pop_back();

        batch = (batch + 1) % num_threads;
    }

    std::atomic<bool> ok = true;
    while (!tasks.empty()) {
        auto argv = ke::PopBack(&tasks);
        auto task =[&ok, argv{std::move(argv)}](ThreadPool*) -> void {
            ok.store(ok.load() & Run(argv));
        };
        cx_->workers().Do(std::move(task));
    }
    cx_->workers().RunCompletionTasks();
    return ok.load();
}

void
Renderer::AddGraphCommands(nlohmann::json& obj, const std::vector<GraphCommand>& commands,
                           const Date& date)
{
    for (const auto& [race_type, graph_type] : commands) {
        auto date_str = ke::StringPrintf("%d-%d-%d", date.month(), date.day(), date.year());
        auto path = SuffixedName("graph-" + race_type + "-" + graph_type + ".svg", date);

        std::vector<std::string> argv;
        argv.emplace_back(graph_type);
        argv.emplace_back(race_type);
        argv.emplace_back(std::move(date_str));
        argv.emplace_back(OutputPath(path));
        AddGraphCommands(std::move(argv));

        obj[race_type + "_" + graph_type + "_img"] = path;
    }
}

void
Renderer::AddGraphCommands(std::vector<std::string>&& commands)
{
    std::unique_lock<std::mutex> lock(lock_);
    graph_commands_.emplace_back(std::move(commands));
}

bool
Renderer::CopyNonTemplateFiles()
{
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(dir_.c_str()), closedir);
    if (!dir) {
        PErr() << "opendir " << dir_ << " failed";
        return false;
    }

    struct dirent* dt;
    while ((dt = readdir(dir.get())) != nullptr) {
        if (dt->d_name == "."s || dt->d_name == ".."s)
            continue;
        if (ke::EndsWith(dt->d_name, ".tpl"))
            continue;

        auto path = dir_ + "/" + dt->d_name;
        auto dest_path = out_ + "/" + dt->d_name;
        if (MustRegenFile(path, dest_path)) {
            std::string contents;
            if (!ReadFile(path, &contents)) {
                PErr() << "read " << path << " failed";
                return false;
            }
            if (!SaveFile(contents, dest_path)) {
                PErr() << "write " << dest_path << " failed";
                return false;
            }
            Out() << "copied " << path << " to " << dest_path;
        }
    }
    return true;
}

void
Renderer::RenderTo(const std::string& tpl, const nlohmann::json& obj, const std::string& path)
{
    std::string text = Render(tpl, obj);
    Save(path, text);

    Out() << "Rendered " << tpl << " to " << path;
}

std::string
Renderer::Render(const std::string& tpl, const nlohmann::json& obj)
{
    std::string text;
    {
        std::lock_guard<std::mutex> lock(lock_);

        auto iter = doc_cache_.find(tpl);
        if (iter == doc_cache_.end()) {
            std::string doc;
            if (!ReadFile(dir_ + "/" + tpl, &doc))
                abort();
            doc_cache_.emplace(tpl, std::move(doc));
            iter = doc_cache_.find(tpl);
        }

        // :TODO: no copies here.
        text = iter->second;
    }

    return env_.render(text, obj);
}

HtmlGenerator::HtmlGenerator(Renderer* renderer, const ModelData& data, const ModelData* prev_data)
  : renderer_(renderer),
    campaign_(renderer->campaign_data()),
    data_(data),
    prev_data_(prev_data)
{
    is_prediction_ = data_.date() <= campaign_.election_day();
}

static bool
HumanReadableDateTime(int64_t ts, std::string* text)
{
    time_t raw_time = (const time_t)ts;
    struct tm dt;

    raw_time = UtcToLocal(raw_time);
    if (!localtime_r(&raw_time, &dt)) {
        PErr() << "localtime failed";
        return false;
    }
    *text = ke::FormatTime(dt, "%b %e, %Y %I:%M:%S%p ") + gTimezoneName;
    return true;
}

static bool
HumanReadableDate(const Date& date, std::string* text)
{
    struct tm tm = {};
    if (!ConvertDate(date, &tm)) {
        Err() << "Could not convert date";
        return false;
    }
    *text = ke::FormatTime(tm, "%b %e, %Y");
    return true;
}

static inline bool
IsSlimMargin(double d)
{
    return fabs(d) > 0.0 && fabs(d) < 0.1;
}

static std::string
DoubleToString(double d, bool is_precise = false)
{
    if (is_precise && IsSlimMargin(d)) {
        auto temp = ke::StringPrintf("%.6f", d);
        while (ke::EndsWith(temp, "0") && !ke::EndsWith(temp, ".0"))
            temp.pop_back();
        return temp;
    }
    return ke::StringPrintf("%.1f", d);
}

static std::vector<std::tuple<double, std::string, std::string>> kDemMarginColors = {
    {10.0, "#0000ff", "dem"},
    {5.0,  "#3399ff", "maybe_dem"},
    {0.0,  "#99ccff", "leans_dem"},
};
static std::vector<std::tuple<double, std::string, std::string>> kGopMarginColors = {
    {10.0, "#ff0000", "gop"},
    {5.0,  "#ec7063", "maybe_gop"},
    {0.0,  "#f5b7b1", "leans_gop"},
};

static std::pair<std::string, std::string>
GetColorForMargin(double margin)
{
    auto& colors = (margin > 0) ? kDemMarginColors : kGopMarginColors;
    auto abs_margin = fabs(margin);
    for (const auto& [limit, color, clazz] : colors) {
        if (abs_margin >= limit)
            return {color, clazz};
    }
    return {"#000000", "none"};
}

void
HtmlGenerator::AddPollWinner(nlohmann::json& obj, const std::string& prefix,
                             const RaceModel& model)
{
    return AddWinner(obj, prefix, model.margin(), !is_prediction_);
}

void
HtmlGenerator::AddWinner(nlohmann::json& obj, const std::string& prefix, double value,
                         bool is_precise, bool allow_tbd)
{
    if (value == 0 || (!is_precise && IsSlimMargin(value)) ||
        (is_wrongometer_ && value < 1.0 && value > -1.0))
    {
        obj[prefix + "_class"] = "tie";
        if (is_wrongometer_)
            obj[prefix + "_text"] = "Tie";
        else if (is_prediction_ || !allow_tbd)
            obj[prefix + "_text"] = "Even";
        else
            obj[prefix + "_text"] = "TBD";
    } else {
        if (value > 0)
            obj[prefix + "_text"] = "D+" + DoubleToString(value, is_precise);
        else if (value < 0)
            obj[prefix + "_text"] = "R+" + DoubleToString(abs(value), is_precise);
        obj[prefix + "_class"] = GetColorForMargin(value).second;
    }
}

void
HtmlGenerator::AddWinnerRating(nlohmann::json& obj, const std::string& prefix,
                               const RaceModel& model, double safe_zone)
{
    if (model.rating().empty())
        return;

    std::string text, text_suffix, class_prefix, class_suffix;

    auto parts = ke::Split(model.rating(), " ");
    if (parts[0] == "tossup") {
        text = "Tossup";
    } else if (parts[0] == "leans") {
        text = "Leans";
        class_prefix = "leans_";
    } else if (parts[0] == "safe") {
        text = "Safe";
    } else if (parts[0] == "likely") {
        text = "Likely";
        class_prefix = "maybe_";
    } else {
        return;
    }
    if (parts.size() == 2 && parts[1] == "dem") {
        class_suffix = "dem";
        text_suffix = " D";
    } else if (parts.size() == 2 && parts[1] == "gop") {
        class_suffix = "gop";
        text_suffix = " R";
    } else {
        class_suffix = "tie";
    }
    obj[prefix + "_class"] = class_prefix + class_suffix;
    obj[prefix + "_text"] = text + text_suffix;
}

bool
HtmlGenerator::AddMapEv(const std::string& prefix, const MapEv& evs, bool no_ties,
                        const std::string& dem, const std::string& gop)
{
    nlohmann::json subobj;
    if (dem.empty())
        subobj["dem_name"] = "D";
    else
        subobj["dem_name"] = dem;
    if (gop.empty())
        subobj["gop_name"] = "R";
    else
        subobj["gop_name"] = gop;
    subobj["dem"] = evs.dem();
    subobj["gop"] = evs.gop();
    if (!no_ties)
        subobj["ties"] = renderer_->total_evs() - evs.dem() - evs.gop();
    else
        subobj["ties"] = 0;

    main_[prefix] = renderer_->Render("ev_line.tpl", subobj);
    return true;
}

static std::vector<RaceModel>
BuildStateList(const google::protobuf::RepeatedPtrField<RaceModel>& entries)
{
    std::vector<RaceModel> list;
    for (const auto& entry : entries)
        list.emplace_back(entry);
    return list;
}

static inline double
RoundToTenth(double margin)
{
    return std::round(margin * 10.0f) / 10.f;
}

void
HtmlGenerator::RenderDelta(nlohmann::json& obj, double prev_margin, double new_margin)
{
    double color_sign = 0.0;

    // If we get something like 0.15 - 0.08, the delta will be 0.07, which is
    // not enough to show a difference. So instead, we round each margin to
    // the nearest tenth, then find that difference.
    double abs_delta = abs(RoundToTenth(new_margin) - RoundToTenth(prev_margin));

    // Multiply by 10 and round to account for numbers like 0.9999999.
    double rounded_delta = RoundToNearest(abs_delta * 10.0);
    if (rounded_delta > -1.0 && rounded_delta < 1.0)
        return;

    std::string dt_value;
    std::string dt_class;
    if (prev_margin == 0) {
        // The new party gained support.
        color_sign = new_margin;
    } else if (new_margin > prev_margin) {
        // Dems gained.
        color_sign = 1;
    } else if (new_margin < prev_margin) {
        // Gop gained.
        color_sign = -1;
    }

    std::string prefix;
    if (color_sign > 0) {
        dt_class = "dem";
        dt_value = "+" + DoubleToString(abs_delta, !is_prediction_);
        if (data_.date() <= campaign_.election_day())
            prefix = "D";
    } else if (color_sign < 0) {
        dt_class = "gop";
        dt_value = "+" + DoubleToString(abs_delta, !is_prediction_);
        if (data_.date() <= campaign_.election_day())
            prefix = "R";
    }

    obj["dt_class"] = std::move(dt_class);
    obj["dt_value"] = prefix + dt_value;
}

bool
HtmlGenerator::RenderMain(const std::string& path)
{
    nlohmann::json& obj = main_;
    obj["year"] = campaign_.election_day().year();

    obj["for_today"] = (data_.date() == campaign_.history()[0].date());
    obj["backdated"] = renderer_->backdating();
    obj["is_prediction"] = is_prediction_;

    AddNav();

    std::string text;
    if (!HumanReadableDateTime(data_.generated(), &text))
        return false;
    obj["generated"] = text;
    if (!HumanReadableDate(data_.date(), &text))
        return false;
    obj["date"] = text;

    AddWinner(obj, "mm", data_.metamargin(), false, false);
    if (data_.senate_can_flip()) {
        obj["no_senate_mm"] = false;
        AddWinner(obj, "smm", data_.senate_mm(), false, false);
    } else {
        obj["no_senate_mm"] = true;
    }
    if (data_.date() > campaign_.election_day() && prev_data_->senate_can_flip())
        AddWinner(obj, "psmm", prev_data_->senate_mm(), false, false);

    if (data_.house_can_flip()) {
        obj["has_house_mm"] = true;
        AddWinner(obj, "hmm", data_.house_mm(), false, false);
    } else {
        obj["has_house_mm"] = false;
    }
    if (data_.date() > campaign_.election_day() && prev_data_->house_can_flip())
        AddWinner(obj, "phmm", prev_data_->house_mm(), false, false);

    int total_evs = 0;
    for (const auto& state : campaign_.states())
        total_evs += state.evs();

    MapEv mean_ev;
    mean_ev.set_dem(data_.dem_ev_mode());
    mean_ev.set_gop(total_evs - mean_ev.dem());
    AddMapEv("mean_ev", mean_ev, false, campaign_.dem_pres(), campaign_.gop_pres());
    AddMapEv("mean_governor", data_.gov_median(), true);

    if (data_.date() > campaign_.election_day()) {
        MapEv predicted_ev;
        predicted_ev.set_dem(prev_data_->dem_ev_mode());
        predicted_ev.set_gop(total_evs - predicted_ev.dem());
        AddMapEv("predicted_ev", predicted_ev, false, campaign_.dem_pres(), campaign_.gop_pres());
        AddWinner(obj, "pmm", prev_data_->metamargin(), false, false);
        AddMapEv("actual_ev", campaign_.results().evs(), true, campaign_.dem_pres(),
                 campaign_.gop_pres());

        MapEv actual_gov = campaign_.governor_map().seats();
        actual_gov.set_dem(actual_gov.dem() - campaign_.governor_map().seats_up().dem());
        actual_gov.set_gop(actual_gov.gop() - campaign_.governor_map().seats_up().gop());
        for (const auto& model : data_.gov_races()) {
            if (model.margin() > 0.0) {
                actual_gov.set_dem(actual_gov.dem() + 1);
            } else if (model.margin() < 0.0) {
                actual_gov.set_gop(actual_gov.gop() + 1);
            } else {
                Fatal() << "Governor race has no winner: "
                        << campaign_.governor_map().races()[model.race_id()].region();
            }
        }
        AddMapEv("actual_governor", actual_gov, true);

        int gov_change = actual_gov.dem() - campaign_.governor_map().seats().dem();
        RenderSeatChange("agdt", gov_change);
    }

    // Pick a source for predictions.
    const ModelData* src = (data_.date() > campaign_.election_day())
                           ? prev_data_
                           : &data_;

    // Add senate predictions.
    if (src->has_senate_median()) {
        int dem_seats = src->senate_median().dem();

        MapEv ev;
        ev.set_dem(dem_seats);
        ev.set_gop(campaign_.senate().total_seats() - dem_seats);
        AddMapEv("mean_senate", ev, true);

        int senate_dem_change = dem_seats - campaign_.senate().seats().dem();
        RenderSeatChange("sdt", senate_dem_change);

        const EvRange* senate_range = &src->dem_senate_range();
        if (src->has_senate_prediction() && src->senate_prediction().has_score_1sig())
            senate_range = &src->senate_prediction().score_1sig();

        obj["has_senate_data"] = true;
        obj["dem_seats_for_control"] = campaign_.senate().dem_seats_for_control();
        obj["dem_senate_low"] = senate_range->low();
        obj["dem_senate_high"] = senate_range->high();
        if (src->has_senate_prediction()) {
            obj["dem_senate_win_text"] = RenderWinner(src->senate_prediction().dem_win_p());
            obj["dem_senate_win_prob"] = DoubleToString(src->senate_prediction().dem_win_p() * 100);

            // In non-presidential years, having the "alt win prob" is very
            // confusing. For example, in 2014, we get:
            //    R 85% to win >= 50 seats
            //    R 94% to win >= 51 seats
            //
            // This is counter-intuitive. It's actually computed as:
            //    D 15% to win >= 50 seats
            //    D 6% to win >= 51 seats
            //
            // So to correctly display this, we should render:
            //    R 85% to win >= 50 seats
            //    R 94% to win >= 49 seats
            //
            // The only statistic that matters is retaining control, so instead
            // we just don't display the "alt" scenario anymore.
            if (campaign_.presidential_year()) {
                obj["dem_senate_alt_win_prob"] = DoubleToString(src->senate_win_prob_alt() * 100);
                obj["dem_senate_half_seats"] = src->senate_control_alt_seats();
                obj["dem_senate_half_win_text"] = RenderWinner(src->senate_win_prob_alt());
            }
        }

        // Add final senate outcomes.
        if (data_.date() > campaign_.election_day()) {
            MapEv seats = campaign_.senate().seats();
            seats.set_dem(seats.dem() - campaign_.senate().seats_up().dem());
            seats.set_gop(seats.gop() - campaign_.senate().seats_up().gop());

            for (const auto& race : data_.senate_races()) {
                const auto& race_info = campaign_.senate().races()[race.race_id()];
                if (race.margin() > 0.0) {
                    seats.set_dem(seats.dem() + 1);
                } else if (race.margin() < 0.0) {
                    seats.set_gop(seats.gop() + 1);
                } else if (race.too_close_to_call()) {
                    Out() << "WARNING: Senate race is too close to call: " << SeatName(race_info);
                } else {
                    Fatal() << "Senate race has no margin: " << SeatName(race_info);
                }
            }

            AddMapEv("actual_senate", seats, true);
            RenderSeatChange("asdt", seats.dem() - campaign_.senate().seats().dem());
        }
    } else {
        obj["has_senate_data"] = false;
    }

    const EvRange* ec_range = &data_.dem_ev_range();
    if (data_.ec_prediction().has_score_1sig())
        ec_range = &data_.ec_prediction().score_1sig();
    obj["dem_ev_low"] = ec_range->low();
    obj["dem_ev_high"] = ec_range->high();

    if (src->has_house_median()) {
        obj["has_house_data"] = true;

        MapEv house_median = src->house_median();
        AddMapEv("mean_house", house_median, true);

        const EvRange* house_range = &src->dem_house_range();
        if (src->has_house_prediction() && src->house_prediction().has_score_1sig())
            house_range = &src->house_prediction().score_1sig();
        obj["dem_house_low"] = house_range->low();
        obj["dem_house_high"] = house_range->high();

        if (src->has_house_prediction()) {
            obj["dem_house_win_text"] = RenderWinner(src->house_prediction().dem_win_p());
            obj["dem_house_win_prob"] = DoubleToString(src->house_prediction().dem_win_p() * 100);
        } else {
            double win_p = 0.0;
            if (src->house_safe_seats().dem() > src->house_safe_seats().gop())
                win_p = 1.0;
            obj["dem_house_win_text"] = RenderWinner(win_p);
            obj["dem_house_win_prob"] = DoubleToString(win_p * 100.0);
        }

        int dem_change = house_median.dem() - campaign_.house_map().seats().dem();
        RenderSeatChange("hdt", dem_change);

        // Add final house results.
        if (data_.date() > campaign_.election_day()) {
            MapEv totals = data_.house_safe_seats();

            for (const auto& race : data_.house_races()) {
                const auto& race_info = campaign_.house_map().races()[race.race_id()];
                if (race.margin() > 0.0) {
                    totals.set_dem(totals.dem() + 1);
                } else if (race.margin() < 0.0) {
                    totals.set_gop(totals.gop() + 1);
                } else if (race.too_close_to_call()) {
                    Out() << "WARNING: House race is too close to call: " << SeatName(race_info);
                } else {
                    Fatal() << "Margin is even! Race: " << race_info.region();
                }
            }
            AddMapEv("actual_house", totals, true);
            RenderSeatChange("ahdt", totals.dem() - campaign_.house_map().seats().dem());
        }
    } else {
        obj["has_house_data"] = false;
    }

    int dem_change = data_.gov_median().dem() - campaign_.governor_map().seats().dem();
    RenderSeatChange("gdt", dem_change);

    if (campaign_.presidential_year() && !RenderStates())
        return false;
    if (!RenderSenate())
        return false;
    if (!RenderHouse())
        return false;
    if (!RenderNational())
        return false;

    bool has_gov_polls = false;
    for (const auto& race : data_.gov_races()) {
        if (!race.polls().empty()) {
            has_gov_polls = true;
            break;
        }
    }
    obj["has_governor_data"] = has_gov_polls;

    if (has_gov_polls && !RenderGovernor())
        return false;
    
    if (campaign_.presidential_year()) {
        obj["win_evs"] = (renderer_->total_evs() / 2) + 1;
        obj["dem_ec_win_text"] = RenderWinner(data_.ec_prediction().dem_win_p());
        obj["dem_ec_win_prob"] = DoubleToString(data_.ec_prediction().dem_win_p() * 100);

        MapEv map_ev;
        auto map_img = SuffixedName("ec-map.svg", data_.date());
        RenderMap("Electoral Map", false, map_img, &map_ev);
        obj["map_img"] = map_img;

        auto map_img_no_ties = SuffixedName("ec-map-no-ties.svg", data_.date());
        RenderMap("Electoral Map, No Ties", true, map_img_no_ties, &map_ev);
        obj["map_img_no_ties"] = map_img_no_ties;
    }
    obj["is_presidential_year"] = campaign_.presidential_year();
    obj["has_generic_ballot"] = data_.has_generic_ballot();

    std::vector<std::pair<std::string, std::string>> graph_types;
    if (campaign_.presidential_year()) {
        graph_types.emplace_back("president", "bias");
        graph_types.emplace_back("president", "score");
        graph_types.emplace_back("national", "bias");
    }
    if (data_.has_generic_ballot())
        graph_types.emplace_back("generic_ballot", "bias");
    if (data_.senate_can_flip())
        graph_types.emplace_back("senate", "bias");
    if (data_.house_can_flip())
        graph_types.emplace_back("house", "bias");

    renderer_->AddGraphCommands(obj, graph_types, data_.date());

    // Excluse house graphs if we can't build a history.
    auto house_races = &data_.house_races();
    if (data_.date() > campaign_.election_day() && prev_data_)
        house_races = &prev_data_->house_races();

    bool has_house_polls = false;
    for (const auto& race : *house_races) {
        if (!race.polls().empty()) {
            has_house_polls = true;
            break;
        }
    }
    obj["has_house_polls"] = has_house_polls;

    if (IsLatestPrediction())
        renderer_->RenderTo("toplines.html.tpl", obj, "toplines.html");

    renderer_->RenderTo("index.html.tpl", obj, path);
    return true;
}

void
HtmlGenerator::RenderWrongometer()
{
    is_wrongometer_ = true;

    nlohmann::json obj;

    obj["year"] = campaign_.election_day().year();

    auto state_entries = BuildStateList(data_.states());
    std::sort(state_entries.begin(), state_entries.end(), [](const RaceModel& a, const RaceModel& b) -> bool {
        return a.margin() > b.margin();
    });

    int win_evs = GetTiebreakerMajority(renderer_->total_evs());

    int dem_ev = 0;
    int gop_ev = renderer_->total_evs();

    int total_dem_evs = 0;
    int total_gop_evs = 0;

    std::vector<nlohmann::json> states;
    bool added_tipping_point = false;
    for (const auto& state : state_entries) {
        const auto& info = campaign_.states()[state.race_id()];

        nlohmann::json entry;
        entry["name"] = campaign_.states()[state.race_id()].name();
        entry["id"] = state.race_id();
        entry["raw_margin"] = state.margin();
        entry["evs"] = info.evs();
        entry["code"] = info.code();
        AddPollWinner(entry, "margin", state);

        if (dem_ev >= win_evs && !added_tipping_point) {
            entry["class"] = "margin_row_tipping";
            added_tipping_point = true;
        } else {
            entry["class"] = "margin_row_normal";
        }

        if (state.margin() >= 1.0)
            total_dem_evs += info.evs();
        else if (state.margin() <= -1.0)
            total_gop_evs += info.evs();

        entry["gop_ev"] = gop_ev;
        dem_ev += info.evs();
        gop_ev -= info.evs();
        entry["dem_ev"] = dem_ev;

        states.emplace_back(std::move(entry));
    }
    obj["states"] = std::move(states);
    obj["dem_evs"] = total_dem_evs;
    obj["gop_evs"] = total_gop_evs;
    obj["tie_evs"] = renderer_->total_evs() - (total_gop_evs + total_dem_evs);
    obj["dem_pres"] = campaign_.dem_pres();
    obj["gop_pres"] = campaign_.gop_pres();
    obj["total_evs"] = renderer_->total_evs();

    MapEv ignore;
    RenderMap("Electoral Map", false, "wrongometer.svg", &ignore);

    std::string map_contents;
    if (!ReadFile(renderer_->OutputPath("wrongometer.svg"), &map_contents)) {
        Fatal() << "Unable to read file";
    }

    obj["map_svg"] = map_contents;

    renderer_->RenderTo("wrongometer.html.tpl", obj, "wrongometer.html");
}

void
HtmlGenerator::RenderVoteShareGraphs()
{
    nlohmann::json meta_obj;

    auto year_string = std::to_string(campaign_.election_day().year());
    meta_obj["year"] = year_string;

    const auto& date = data_.date();
    auto date_str = ke::StringPrintf("%d-%d-%d", date.month(), date.day(), date.year());

    if (campaign_.presidential_year()) {
        std::vector<nlohmann::json> entries;

        nlohmann::json obj = meta_obj;
        obj["race_type"] = "President";

        for (const auto& model : data_.states()) {
            if (model.polls().empty())
                continue;

            const auto& info = campaign_.states()[model.race_id()];

            nlohmann::json obj;
            obj["region"] = info.name();
            obj["dem_candidate"] = campaign_.dem_pres();
            obj["gop_candidate"] = campaign_.gop_pres();

            auto image_path = "votes-pres-" + info.code() + "-" + year_string + ".png";
            std::vector<std::string> argv = {
                "vote_share",
                "president:" + std::to_string(model.race_id()),
                date_str,
                renderer_->OutputPath(image_path),
            };
            renderer_->AddGraphCommands(std::move(argv));

            obj["graph_image"] = std::move(image_path);

            entries.emplace_back(std::move(obj));
        }

        obj["entries"] = std::move(entries);

        renderer_->RenderTo("vote_shares.html.tpl", obj, "vote_share_states.html");
    }

    // Senate graphs.
    {
        std::vector<nlohmann::json> entries;

        nlohmann::json obj = meta_obj;
        obj["race_type"] = "Senate";

        for (const auto& model : data_.senate_races()) {
            if (model.polls().empty())
                continue;

            const auto& info = campaign_.senate().races()[model.race_id()];

            nlohmann::json obj;
            obj["region"] = info.region();
            obj["dem_candidate"] = info.dem().name();
            obj["gop_candidate"] = info.gop().name();

            auto image_path = "votes-senate-" + std::to_string(model.race_id()) + "-" +
                              year_string + ".png";
            std::vector<std::string> argv = {
                "vote_share",
                "senate:" + std::to_string(model.race_id()),
                date_str,
                renderer_->OutputPath(image_path),
            };
            renderer_->AddGraphCommands(std::move(argv));

            obj["graph_image"] = std::move(image_path);

            entries.emplace_back(std::move(obj));
        }

        obj["entries"] = std::move(entries);

        renderer_->RenderTo("vote_shares.html.tpl", obj, "vote_share_senate.html");
    }
}

bool
HtmlGenerator::RenderStates()
{
    nlohmann::json obj;

    auto state_entries = BuildStateList(data_.states());
    std::sort(state_entries.begin(), state_entries.end(), [](const RaceModel& a, const RaceModel& b) -> bool {
        return a.margin() > b.margin();
    });

    std::unordered_map<int, const RaceModel*> prev_states;
    if (prev_data_) {
        for (const auto& state : prev_data_->states())
            prev_states[state.race_id()] = &state;
    }

    std::vector<nlohmann::json> out_entries;
    int dem_ev = 0;
    int gop_ev = renderer_->total_evs();
    bool added_tipping_point = false;
    for (const auto& state : state_entries) {
        nlohmann::json entry;
        entry["name"] = campaign_.states()[state.race_id()].name();
        AddPollWinner(entry, "margin", state);

        const auto& info = campaign_.states()[state.race_id()];
        entry["gop_ev"] = gop_ev;
        dem_ev += info.evs();
        gop_ev -= info.evs();
        entry["dem_ev"] = dem_ev;
        entry["code"] = info.code();

        if (dem_ev >= 270 && !added_tipping_point) {
            entry["class"] = "margin_row_tipping";
            added_tipping_point = true;
        } else {
            entry["class"] = "margin_row_normal";
        }

        const RepeatedPoll* prev_polls = nullptr;
        if (auto iter = prev_states.find(state.race_id()); iter != prev_states.end()) {
            RenderDelta(entry, iter->second->margin(), state.margin());
            prev_polls = &iter->second->polls();
        }

        if (!AddPollData(entry, state.polls(), prev_polls))
            return false;

        out_entries.emplace_back(std::move(entry));
    }
    obj["entries"] = std::move(out_entries);
    obj["ev_type"] = "EVs";
    obj["race_header_text"] = "State";
    obj["race_has_ev"] = true;
    obj["is_prediction"] = is_prediction_;

    main_["state_table_content"] = renderer_->Render("table.html.tpl", obj);
    return true;
}

bool
HtmlGenerator::RenderSenate()
{
    nlohmann::json obj;

    const SenateMap& senate_map = campaign_.senate();

    std::vector<RaceModel> races;
    int dem_given = 0, gop_given = 0;
    for (const auto& race : data_.senate_races()) {
        // Exclude likely races for which no polling exists.
        if (race.polls().empty() && !race.rating().empty()) {
            if (race.rating() == "gop")
                gop_given++;
            else if (race.rating() == "dem")
                dem_given++;
            continue;
        }
        races.emplace_back(race);
    }

    std::sort(races.begin(), races.end(), [](const RaceModel& a, const RaceModel& b) -> bool {
        return a.margin() > b.margin();
    });

    std::unordered_map<int, const RaceModel*> prev_races;
    if (prev_data_) {
        for (const auto& race : prev_data_->senate_races())
            prev_races[race.race_id()] = &race;
    }

    // Dems count starting from their solid seats. Rs start counting assuming
    // they've won everything dems can lose.
    int dem_seats = senate_map.seats().dem() - senate_map.seats_up().dem() + dem_given;
    int gop_seats = senate_map.seats().gop() + senate_map.seats_up().dem() - dem_given;

    std::vector<nlohmann::json> out_entries;
    bool added_tipping_point = false;
    for (const auto& race: races) {
        const auto& race_info = campaign_.senate().races()[race.race_id()];

        auto iter = kStateCodes.find(race_info.region());
        if (iter == kStateCodes.end()) {
            Fatal() << "Could not find code for state: " << race_info.region();
        }

        nlohmann::json entry;
        entry["name"] = iter->second + ": " + race_info.dem().name() + " (D) - " +
                        race_info.gop().name() + " (R)";
        AddPollWinner(entry, "margin", race);

        entry["gop_ev"] = gop_seats;
        dem_seats++;
        gop_seats--;
        entry["dem_ev"] = dem_seats;
        entry["code"] = "senate_" + std::to_string(race.race_id());

        if (dem_seats >= senate_map.dem_seats_for_control() && !added_tipping_point) {
            entry["class"] = "margin_row_tipping";
            added_tipping_point = true;
        } else {
            entry["class"] = "margin_row_normal";
        }

        const RepeatedPoll* prev_polls = nullptr;
        if (auto iter = prev_races.find(race.race_id()); iter != prev_races.end()) {
            prev_polls = &iter->second->polls();
            if (!race.too_close_to_call() && !prev_polls->empty()) {
                RenderDelta(entry, iter->second->margin(), race.margin());
            }
        }

        if (!AddPollData(entry, race.polls(), prev_polls))
            return false;

        out_entries.emplace_back(std::move(entry));
    }
    obj["entries"] = std::move(out_entries);
    obj["ev_type"] = "Seats";
    obj["race_header_text"] = "Senate Race";
    obj["race_has_ev"] = true;
    obj["is_prediction"] = is_prediction_;

    main_["senate_table_content"] = renderer_->Render("table.html.tpl", obj);
    return true;
}

static std::string
ShortenDistrict(const std::string& district_name)
{
    int num;
    if (auto pos = district_name.rfind(' '); pos != std::string::npos) {
        if (ParseInt(district_name.substr(pos + 1), &num)) {
            if (auto iter = kStateCodes.find(district_name.substr(0, pos));
                iter != kStateCodes.end())
            {
                return iter->second + "-" + std::to_string(num);
            }
        }
    }
    if (auto iter = kStateCodes.find(district_name); iter != kStateCodes.end())
        return iter->second;
    return district_name;
}

static void Dump(const HouseMap& house_map, const ModelData& data)
{
    // This is useful for debugging bad data in the initial .ini files; it will
    // dump the list of flips, which can be corroborated with external sources.
#if 0
    std::vector<const RaceModel*> sorted;
    for (const auto& model : data.house_races())
        sorted.emplace_back(&model);

    std::sort(sorted.begin(), sorted.end(), [&](const RaceModel* a, const RaceModel* b) -> bool {
        const auto& info_a = house_map.races()[a->race_id()];
        const auto& info_b = house_map.races()[b->race_id()];
        return info_a.region() < info_b.region();
    });

    for (const auto& model : sorted) {
        const auto& info = house_map.races()[model->race_id()];
        if (model->margin() > 0.0 && info.current_holder() == "gop") {
            Out() << info.region() << ": FLIP TO D";
        } else if (model->margin() < 0.0 && info.current_holder() == "dem") {
            Out() << info.region() << ": FLIP TO R";
        }
    }
#endif
}

bool
HtmlGenerator::RenderHouse()
{
    nlohmann::json obj;

    const HouseMap& house_map = campaign_.house_map();
    MapEv safe_seats = data_.house_safe_seats();

    if (data_.date() > campaign_.election_day())
        Dump(house_map, data_);

    auto add_implied_seat = [&](const RaceModel& model) -> void {
        if (model.win_prob() >= 0.5)
            safe_seats.set_dem(safe_seats.dem() + 1);
        else if (model.win_prob() < 0.5)
            safe_seats.set_gop(safe_seats.gop() + 1);
    };

    std::deque<const RaceModel*> races;
    for (const auto& race : data_.house_races()) {
        // Skip likely races.
        if (race.polls().empty() && ke::StartsWith(race.rating(), "likely")) {
            add_implied_seat(race);
            continue;
        }
        races.emplace_back(&race);
    }

    // Did the previous day have no polls? If so, we skip showing any error column.
    if (!is_prediction_) {
        bool no_polls = true;
        for (const auto& race : prev_data_->house_races()) {
            if (!race.polls().empty()) {
                no_polls = false;
                break;
            }
        }
        if (no_polls)
            obj["skip_error"] = true;
        obj["show_rating"] = true;
    }

    // This is a complicated list to display, so we try to stable sort it.
    // If either race has no polls, we sort by win probability. If they're
    // equal, and one has polls, the non-polled race come first (so "Tossup"
    // and "Even" races aren't interleaved). If both races are polled, we
    // sort by margin.
    //
    // If everything else is equal, we sort by district name, which is unique.
    std::sort(races.begin(), races.end(), [&](const RaceModel* a, const RaceModel* b) -> bool {
        if (a->polls().empty() || b->polls().empty()) {
            if (a->win_prob() > b->win_prob())
                return true;
            if (a->win_prob() < b->win_prob())
                return false;
            if (a->polls().empty() && !b->polls().empty())
                return true;
            if (!a->polls().empty() && b->polls().empty())
                return false;
        } else {
            if (a->margin() > b->margin())
                return true;
            if (a->margin() < b->margin())
                return false;
        }
        return house_map.races()[a->race_id()].region() <
               house_map.races()[b->race_id()].region();
    });

    std::unordered_map<int, const RaceModel*> prev_races;
    if (prev_data_) {
        for (const auto& race : prev_data_->house_races())
            prev_races[race.race_id()] = &race;
    }

    // After election day, trim seats that are not interesting, pruning any
    // from the D and R long tail that had no ratings, and were > the
    // metamargin.
    if (!is_prediction_) {
        std::optional<double> house_mm;
        if (data_.house_can_flip())
            house_mm = {data_.house_mm()};

        auto filter_race = [&](const RaceModel& model) -> bool {
            auto iter = prev_races.find(model.race_id());
            const RaceModel* prev_model = (iter == prev_races.end()) ? nullptr : iter->second;

            if (!prev_model ||
                (prev_model->rating().empty() || ke::StartsWith(prev_model->rating(), "safe")))
            {
                if (!house_mm)
                    return true;
                if (abs(model.margin()) > abs(*house_mm) + 2.0)
                    return true;
            }
            return false;
        };

        while (!races.empty()) {
            if (!filter_race(*races.front()))
                break;
            add_implied_seat(*races.front());
            races.pop_front();
        }
        while (!races.empty()) {
            if (!filter_race(*races.back()))
                break;
            add_implied_seat(*races.back());
            races.pop_back();
        }
    }

    // Dems count starting from their solid seats. Rs start counting assuming
    // they've won everything dems can lose.
    int dem_seats = safe_seats.dem();
    int gop_seats = house_map.total_seats() - safe_seats.dem();

    int midpoint = house_map.total_seats() / 2;
    if (midpoint * 2 <= house_map.total_seats())
        midpoint++;

    std::vector<nlohmann::json> out_entries;
    bool added_tipping_point = false;
    for (const auto& race: races) {
        const auto& race_info = house_map.races()[race->race_id()];

        nlohmann::json entry;
        if (race_info.dem().name().empty() && race_info.gop().name().empty()) {
            entry["name"] = race_info.region();
        } else {
            entry["name"] = ShortenDistrict(race_info.region()) + ": " + race_info.dem().name() +
                            " (D) - " + race_info.gop().name() + " (R)";
        }
        if (!race->polls().empty()) {
            AddPollWinner(entry, "margin", *race);
        } else if (is_prediction_) {
            // Only show the rating if no margin is available.
            AddWinnerRating(entry, "margin", *race);
        } else {
            Fatal() << "Race " << race_info.region() << " has no margin";
            return false;
        }

        entry["gop_ev"] = gop_seats;
        dem_seats++;
        gop_seats--;
        entry["dem_ev"] = dem_seats;
        entry["code"] = "house_" + std::to_string(race->race_id());

        if (dem_seats >= midpoint && !added_tipping_point) {
            entry["class"] = "margin_row_tipping";
            added_tipping_point = true;
        } else {
            entry["class"] = "margin_row_normal";
        }

        const RepeatedPoll* prev_polls = nullptr;
        if (prev_data_) {
            // Since house races are added incrementally, show a margin change
            // even if there was no previous data.
            const RaceModel* prev_race = nullptr;
            double prev_margin = 0.0f;
            if (auto iter = prev_races.find(race->race_id()); iter != prev_races.end()) {
                prev_race = &*iter->second;
                prev_polls = &prev_race->polls();
                prev_margin = prev_race->margin();
            }

            if (prev_race && race->rating() != prev_race->rating() &&
                !prev_race->rating().empty() && race->polls().empty())
            {
                if (prev_race->win_prob() < race->win_prob()) {
                    entry["dt_value"] = "Toward D";
                    entry["dt_class"] = "tie";
                } else if (prev_race->win_prob() > race->win_prob()) {
                    entry["dt_value"] = "Toward R";
                    entry["dt_class"] = "tie";
                }
            } else if (!race->too_close_to_call() && prev_polls && !prev_polls->empty()) {
                RenderDelta(entry, prev_margin, race->margin());
            }

            // After election day, include the final rating.
            if (prev_race && !is_prediction_)
                AddWinnerRating(entry, "rating", *prev_race);
        }

        if (!AddPollData(entry, race->polls(), prev_polls))
            return false;

        out_entries.emplace_back(std::move(entry));
    }
    obj["entries"] = std::move(out_entries);
    obj["ev_type"] = "Seats";
    obj["race_header_text"] = "House Race";
    obj["race_has_ev"] = true;
    obj["is_prediction"] = is_prediction_;

    main_["house_table_content"] = renderer_->Render("table.html.tpl", obj);
    return true;
}

bool
HtmlGenerator::RenderNational()
{
    std::vector<nlohmann::json> out_entries;

    if (!data_.has_generic_ballot()) {
        main_["other_table_content"] = "";
        return true;
    }

    if (campaign_.presidential_year()) {
        nlohmann::json entry;
        entry["name"] = "National Average";
        AddPollWinner(entry, "margin", data_.national());
        entry["code"] = "national";
        entry["class"] = "margin_row_normal";

        const RepeatedPoll* prev_polls = nullptr;
        if (prev_data_) {
            RenderDelta(entry, prev_data_->national().margin(), data_.national().margin());
            prev_polls = &prev_data_->national().polls();
        }

        if (!AddPollData(entry, data_.national().polls(), prev_polls))
            return false;
        out_entries.emplace_back(std::move(entry));
    }

    {
        const auto& race = data_.generic_ballot();

        nlohmann::json entry;
        entry["name"] = "Generic Ballot";
        AddPollWinner(entry, "margin", race);
        entry["code"] = "generic_ballot";
        entry["class"] = "margin_row_normal";

        const RepeatedPoll* prev_polls = nullptr;
        if (prev_data_) {
            RenderDelta(entry, prev_data_->generic_ballot().margin(), race.margin());
            prev_polls = &prev_data_->generic_ballot().polls();
        }

        if (!AddPollData(entry, race.polls(), prev_polls))
            return false;
        out_entries.emplace_back(std::move(entry));
    }

    nlohmann::json obj;
    obj["entries"] = std::move(out_entries);
    obj["race_header_text"] = "";
    obj["race_has_ev"] = false;
    obj["is_prediction"] = is_prediction_;

    main_["other_table_content"] = renderer_->Render("table.html.tpl", obj);
    return true;
}

bool
HtmlGenerator::RenderGovernor()
{
    nlohmann::json obj;

    const GovernorMap& governor_map = campaign_.governor_map();

    std::vector<RaceModel> races;
    int dem_given = 0, gop_given = 0;
    for (const auto& race : data_.gov_races()) {
        // Exclude likely races for which no polling exists.
        if (race.polls().empty() && !race.rating().empty()) {
            if (race.rating() == "gop")
                gop_given++;
            else if (race.rating() == "dem")
                dem_given++;
            continue;
        }
        races.emplace_back(race);
    }

    std::sort(races.begin(), races.end(), [](const RaceModel& a, const RaceModel& b) -> bool {
        return a.margin() > b.margin();
    });

    std::unordered_map<int, const RaceModel*> prev_races;
    if (prev_data_) {
        for (const auto& race : prev_data_->gov_races())
            prev_races[race.race_id()] = &race;
    }

    // Dems count starting from their solid seats. Rs start counting assuming
    // they've won everything dems can lose.
    int dem_seats = governor_map.seats().dem() - governor_map.seats_up().dem() + dem_given;
    int gop_seats = governor_map.seats().gop() + governor_map.seats_up().dem() - dem_given;

    std::vector<nlohmann::json> out_entries;
    for (const auto& race: races) {
        const auto& race_info = campaign_.governor_map().races()[race.race_id()];

        auto iter = kStateCodes.find(race_info.region());
        if (iter == kStateCodes.end()) {
            Fatal() << "Could not find code for state: " << race_info.region();
        }

        nlohmann::json entry;
        entry["name"] = iter->second + ": " + race_info.dem().name() + " (D) - " +
                        race_info.gop().name() + " (R)";
        AddPollWinner(entry, "margin", race);

        entry["gop_ev"] = gop_seats;
        dem_seats++;
        gop_seats--;
        entry["dem_ev"] = dem_seats;
        entry["code"] = "governor_" + std::to_string(race.race_id());

        // No tipping point, there is no body or congress of governors.
        entry["class"] = "margin_row_normal";

        const RepeatedPoll* prev_polls = nullptr;
        if (auto iter = prev_races.find(race.race_id()); iter != prev_races.end()) {
            prev_polls = &iter->second->polls();
            if (!race.too_close_to_call() && !prev_polls->empty()) {
                RenderDelta(entry, iter->second->margin(), race.margin());
            }
        }

        if (!AddPollData(entry, race.polls(), prev_polls))
            return false;

        out_entries.emplace_back(std::move(entry));
    }
    obj["entries"] = std::move(out_entries);
    obj["ev_type"] = "Seats";
    obj["race_header_text"] = "Governor Race";
    obj["race_has_ev"] = true;
    obj["is_prediction"] = is_prediction_;

    main_["governor_table_content"] = renderer_->Render("table.html.tpl", obj);
    return true;
}

void
HtmlGenerator::AddNav()
{
    const auto& latest_date = campaign_.history()[0].date();

    nlohmann::json nav;
    if (prev_data_)
        nav["prev_url"] = SuffixedName("index.html", prev_data_->date());
    else
        nav["prev_url"] = "";

    nav["next_is_final_results"] = false;
    if (data_.date() != latest_date) {
        nav["next_url"] = SuffixedName("index.html", data_.date() + 1);
        nav["next_is_final_results"] = latest_date > campaign_.election_day() &&
                                       data_.date() + 1 == latest_date;
    } else {
        nav["next_url"] = "";
    }

    main_["nav_text"] = renderer_->Render("nav.tpl", nav);
}

void
HtmlGenerator::RenderMap(const std::string& title, bool no_ties, const std::string& path,
                         MapEv* evs)
{
    nlohmann::json obj;

    *evs = {};

    obj["width"] = 959;
    obj["height"] = 593;

    std::vector<nlohmann::json> out_entries;
    for (const auto& state : data_.states()) {
        const auto& info = campaign_.states()[state.race_id()];
        auto title_key = info.name();
        std::replace(title_key.begin(), title_key.end(), ' ', '_');

        std::string margin_text;
        bool is_tie = false;
        if (state.margin() == 0 || (is_prediction_ && IsSlimMargin(state.margin())) ||
            (is_wrongometer_ && state.margin() < 1.0 && state.margin() > -1.0))
        {
            if (is_wrongometer_)
                margin_text = "Tie";
            else
                margin_text = "Even";
            is_tie = true;
        } else if (state.margin() > 0) {
            margin_text = "D+" + DoubleToString(state.margin(), !is_prediction_);
        } else if (state.margin() < 0) {
            margin_text = "R+" + DoubleToString(abs(state.margin()), !is_prediction_);
        }

        if (is_wrongometer_) {
            obj[title_key] = info.name() + " (" + std::to_string(info.evs()) + " EVs)";
        } else {
            obj[title_key] = info.name() + " - " + margin_text + " (" + std::to_string(info.evs()) +
                             " EVs)";
        }

        if (is_tie)
            continue;

        if (state.margin() > kSafeMargin || (!no_ties && state.margin() > 0))
            evs->set_dem(evs->dem() + info.evs());
        else if (state.margin() < -kSafeMargin || (!no_ties && state.margin() < 0))
            evs->set_gop(evs->gop() + info.evs());

        nlohmann::json entry;
        entry["code"] = info.code();
        if (!state.margin() && !no_ties)
            entry["color"] = "#d3d3d3";
        else
            entry["color"] = GetColorForMargin(state.margin()).first;
        out_entries.emplace_back(std::move(entry));
    }
    obj["state_entries"] = std::move(out_entries);
    obj["title"] = title;

    renderer_->RenderTo("us_map.svg.tpl", obj, path);
}

bool
HtmlGenerator::BuildPollRows(const std::vector<const Poll*>& polls, const std::string& icon,
                             std::vector<nlohmann::json>* out)
{
    for (const auto& ppoll : polls) {
        const auto& poll = *ppoll;
        nlohmann::json pe;
        pe["icon"] = icon;
        pe["description"] = poll.description();

        std::string text;
        if (!HumanReadableDate(poll.start(), &text))
            return false;
        pe["start"] = text;
        if (!HumanReadableDate(poll.end(), &text))
            return false;
        pe["end"] = text;

        if (poll.dem())
            pe["dem"] = DoubleToString(poll.dem());
        else
            pe["dem"] = "";
        if (poll.gop())
            pe["gop"] = DoubleToString(poll.gop());
        else
            pe["gop"] = "";
        pe["url"] = poll.url();
        pe["weight"] = poll.weight();
        AddWinner(pe, "margin", RoundMargin(poll.margin()),
                  (!is_prediction_ && icon == "new") /* is_precise */,
                  icon == "new" /* allow_tbd */);
        out->emplace_back(std::move(pe));
    }
    return true;
}

bool
HtmlGenerator::AddPollData(nlohmann::json& obj, const RepeatedPoll& polls,
                           const RepeatedPoll* prev_polls)
{
    std::vector<const Poll*> new_polls, old_polls, aged_polls;

    std::unordered_set<std::string> new_set, old_set;
    for (const auto& poll : polls)
        new_set.emplace(poll.id());
    if (prev_polls) {
        for (const auto& poll : *prev_polls) {
            old_set.emplace(poll.id());
            if (!new_set.count(poll.id()))
                aged_polls.emplace_back(&poll);
        }
    }

    for (const auto& poll : polls) {
        if (old_set.count(poll.id()))
            old_polls.emplace_back(&poll);
        else
            new_polls.emplace_back(&poll);
    }

    std::vector<nlohmann::json> rows;
    if (!BuildPollRows(new_polls, "new", &rows))
        return false;
    if (!BuildPollRows(old_polls, "", &rows))
        return false;
    if (!BuildPollRows(aged_polls, "old", &rows))
        return false;

    obj["polls"] = rows;
    return true;
}

std::string
HtmlGenerator::RenderWinner(double raw_win_p)
{
    nlohmann::json obj;

    // Truncate, don't round.
    int dem_win_p = (int)(raw_win_p * 100.0f);
    int win_p = dem_win_p;

    std::string text, text_suffix;
    std::string css_class, css_class_prefix;
    if (dem_win_p > 50) {
        css_class = "dem";
        text_suffix = " D";
    } else if (dem_win_p < 50) {
        css_class = "gop";
        text_suffix = " R";
        win_p = 100 - dem_win_p;
    }

    if (win_p >= 99) {
        text = "Safe";
    } else if (win_p >= 90) {
        text = "Very Likely";
    } else if (win_p >= 80) {
        text = "Likely";
        css_class_prefix = "maybe_";
    } else if (win_p >= 65) {
        text = "Leans";
        css_class_prefix = "leans_";
    } else {
        css_class = "tie";
        text = "Tossup";
        text_suffix = "";
    }

    obj["text"] = text + text_suffix;
    obj["class"] = css_class_prefix + css_class;
    obj["raw_value"] = std::to_string(dem_win_p);

    return renderer_->Render("win_line.html.tpl", obj);
}

void
HtmlGenerator::RenderSeatChange(const std::string& prefix, int change)
{
    auto class_key = prefix + "_class";
    auto text_key = prefix + "_text";
    auto suffix = abs(change) > 1 ? "s"s : ""s;
    if (change > 0 ){
        main_[class_key] = "dem";
        main_[text_key] = "D +" + std::to_string(change) + " seat" + suffix;
    } else if (change < 0) {
        main_[class_key] = "gop";
        main_[text_key] = "R +" + std::to_string(-change) + " seat" + suffix;
    } else {
        main_[class_key] = "tie";
        main_[text_key] = "No net change";
    }
}

bool
HtmlGenerator::IsLatestPrediction()
{
    if (!is_prediction_)
        return false;
    if (campaign_.history()[0].date() > campaign_.election_day())
        return data_.date() == campaign_.history()[1].date();
    return data_.date() == campaign_.history()[0].date();
}

} // namespace stone
