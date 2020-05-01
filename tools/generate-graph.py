# vim: set sts=4 ts=8 sw=4 tw=99 et:
import argparse
import datetime
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np
import sys

import state_pb2
import history_pb2

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('history_file', type=str, help='History file')
    parser.add_argument('graph_type', type=str, help='Graph type or batch mode',
                        choices=['bias', 'score', 'batch'])
    args = parser.parse_args(sys.argv[1:3])

    cd = history_pb2.CampaignData()
    with open(args.history_file, 'rb') as fp:
        cd.ParseFromString(fp.read())

    commands = []
    if args.graph_type == 'batch':
        cursor = 3
        args_per_batch = 4
        while True:
            subargs = sys.argv[cursor : cursor + args_per_batch]
            if len(subargs) == 0:
                break
            parser = argparse.ArgumentParser()
            parser.add_argument('graph_type', type=str, help='Graph type')
            parser.add_argument('race_type', type=str, help='Race type')
            parser.add_argument('end_date', type=str, help='End date')
            parser.add_argument('output_file', type=str, help='Output file pattern')
            xargs = parser.parse_args(subargs)
            commands.append((xargs.graph_type, xargs.race_type, xargs.end_date, xargs.output_file))
            cursor += args_per_batch
    else:
        parser = argparse.ArgumentParser()
        parser.add_argument('race_type', type=str, help='Race type')
        parser.add_argument('end_date', type=str, help='End date')
        parser.add_argument('--output-file', type=str, help='Output file pattern')
        xargs = parser.parse_args(sys.argv[3:])
        commands.append((args.graph_type, xargs.race_type, xargs.end_date, xargs.output_file))

    if len(commands) == 0:
        raise Exception('No commands given')

    for graph_type, race_type, end_date_str, output_file in commands:
        parts = end_date_str.split('-')
        if len(parts) != 3:
            raise Exception('Date format must be M-D-Y')
        parts = [int(part) for part in parts]
        end_date = datetime.date(parts[2], parts[0], parts[1])

        if graph_type == 'score':
            grapher = ScoreGrapher(cd, end_date, race_type)
        elif graph_type == 'bias':
            grapher = BiasGrapher(cd, end_date, race_type)
        else:
            raise Exception('Unknown graph type: {}'.format(graph_type))

        fig = grapher.plot()
        if output_file is None:
            plt.show()
            return 0

        fig.savefig(output_file)
        print('Rendered {}'.format(output_file))

        plt.close(fig)

    return 0

def get_proto_date(d):
    return datetime.date(d.year, d.month, d.day)

class Grapher(object):
    def __init__(self, cd, end_date, race_type):
        self.cd_ = cd
        self.end_date_ = end_date
        self.race_type_ = race_type
        self.election_day_ = get_proto_date(self.cd_.election_day)

        self.datapoints_ = []
        for datapoint in self.cd_.history:
            date = get_proto_date(datapoint.date)
            if date > self.end_date_ or date > self.election_day_:
                continue
            self.datapoints_.append(datapoint)
        self.datapoints_.reverse()

    def config(self, fig, ax):
        ax.grid(True)

        x_min = get_proto_date(self.cd_.history[-1].date)
        x_max = self.election_day_
        ax.set_xlim(x_min, x_max)

        ax.xaxis.set_major_locator(mdates.MonthLocator())
        ax.xaxis.set_major_formatter(mdates.DateFormatter('%b'))
        fig.autofmt_xdate()

        ax.legend()
        if self.race_type_ in ['president', 'national', 'generic_ballot', 'senate']:
            self.add_important_dates(ax)

    def add_important_dates(self, ax):
        y_min, y_max = ax.get_ybound()

        prev_date = None
        for important_date in self.cd_.important_dates:
            date = get_proto_date(important_date.date)
            ax.axvline(date,
                       color = 'maroon',
                       linestyle = 'dashed',
                       linewidth = 1.0,
                       zorder = 0)
            y_offset = 0
            if prev_date and date - prev_date <= datetime.timedelta(7):
                y_offset += 0.1 * (y_max - y_min)
            ax.text(date, y_min + y_offset, important_date.label, rotation = 45)
            prev_date = date

    @property
    def race_title(self):
        return Grapher.kRaceTitles[self.race_type_]

    @property
    def bias_type(self):
        if self.race_type_ in ['national', 'generic_ballot']:
            return 'Polls'
        return 'Bias'

    kRaceTitles = {
        'president': 'Electoral College',
        'senate': 'Senate',
        'house': 'House',
        'national': 'Presidential',
        'generic_ballot': 'Generic Ballot',
    }

class BiasGrapher(Grapher):
    def __init__(self, args, cd, end_date):
        super(BiasGrapher, self).__init__(args, cd, end_date)

    def plot(self):
        fig, ax = plt.subplots()

        accessor = BiasGrapher.kBiasAccessors[self.race_type_]

        x_data = []
        y_data = []
        for dp in self.datapoints_:
            y_data.append(accessor(dp))
            x_data.append(get_proto_date(dp.date))

        label = None
        if self.race_type_ in ['national', 'generic_ballot']:
            label = 'Two-Party Margin'
        else:
            label = 'Bias'

        ax.fill_between([x_data[0], get_proto_date(self.cd_.election_day)], 0, -100,
                        color = '#fdd7e4', alpha = 0.50, zorder = 3)
        ax.fill_between([x_data[0], get_proto_date(self.cd_.election_day)], 0, 100,
                        color = '#c2dfff', alpha = 0.50, zorder = 3)
        ax.plot(x_data, y_data, zorder = 4, color = '#000000', label = label)

        ax.set_title("History of {} {} {}".format(self.cd_.election_day.year, self.race_title,
                     self.bias_type))
        if self.race_type_ in ['national', 'generic_ballot']:
            ax.set_ylabel("{} Polling Average".format(self.race_title))
        else:
            ax.set_ylabel("{} Bias".format(self.race_title))

        score = None
        if self.end_date_ >= self.election_day_:
            score = self.add_final_results(ax)

        y_min = min(np.amin(y_data) - 2.5, -2.5)
        y_max = max(np.amax(y_data) + 2.5, 2.5)
        if score is not None:
            y_min = min(y_min, score - 1.0)
            y_max = max(y_max, score + 1.0)
        ax.set_ybound(y_min, y_max)

        # Must be after setting bounds/limits.
        self.config(fig, ax)
        return fig

    def add_final_results(self, ax):
        latest = self.cd_.history[0]
        if get_proto_date(latest.date) <= self.election_day_:
            return

        if self.race_type_ == 'national':
            score = latest.national.margin
        elif self.race_type_ == 'generic_ballot':
            score = latest.generic_ballot.margin
        elif self.race_type_ == 'senate':
            score = latest.senate_mm
        elif self.race_type_ == 'house':
            score = latest.house_mm
        elif self.race_type_ == 'president':
            score = latest.metamargin
        else:
            return

        if score > 0:
            winner_color = 'blue'
        elif score < 0:
            winner_color = 'red'
        else:
            winner_color = 'black'

        ax.plot([self.election_day_], [score], marker = '*', markersize = 10,
                color = winner_color, clip_on = False, zorder = 4)
        return score

    @property
    def race_title(self):
        return Grapher.kRaceTitles[self.race_type_]

    @property
    def type(self):
        return 'bias'

    kBiasAccessors = {
        'president': lambda model: model.metamargin,
        'senate': lambda model: model.senate_mm,
        'house': lambda model: model.house_mm,
        'national': lambda model: model.national.margin,
        'generic_ballot': lambda model: model.generic_ballot.margin,
    }

def get_pres_score(model):
    mode_ev = state_pb2.MapEv()
    mode_ev.dem = model.dem_ev_mode
    mode_ev.gop = (model.mean_ev.dem + model.mean_ev.gop) - mode_ev.dem
    return mode_ev, model.dem_ev_range

class ScoreGrapher(Grapher):
    def __init__(self, args, cd, end_date):
        super(ScoreGrapher, self).__init__(args, cd, end_date)

    def plot(self):
        fig, ax = plt.subplots()

        accessor = ScoreGrapher.kScoreAccessors[self.race_type_]

        x_data = []
        dem_data, gop_data = [], []
        dem_min, dem_max = [], []
        gop_min, gop_max = [], []
        for dp in self.datapoints_:
            score, dem_range = accessor(dp)
            x_data.append(get_proto_date(dp.date))
            dem_data.append(score.dem)
            gop_data.append(score.gop)
            dem_min.append(dem_range.low)
            dem_max.append(dem_range.high)
            gop_min.append((score.dem + score.gop) - dem_range.low)
            gop_max.append((score.dem + score.gop) - dem_range.high)

        y_min = 0
        y_max = dem_data[-1] + gop_data[-1]
        ax.set_ybound(y_min, y_max)

        dem_label, gop_label = None, None
        if self.race_type_ == 'president':
            dem_label = self.cd_.dem_pres
            gop_label = self.cd_.gop_pres

        ax.fill_between(x_data, gop_min, gop_max, color = '#fdd7e4', alpha = 0.5, zorder = 1)
        ax.fill_between(x_data, dem_min, dem_max, color = '#c2dfff', alpha = 0.5, zorder = 1)
        ax.plot(x_data, dem_data, color = 'blue', zorder = 3, label = dem_label)
        ax.plot(x_data, gop_data, color = 'red', zorder = 3, label = gop_label)
        ax.axhline(y_max / 2, color = 'black', linestyle = 'dashed', zorder = 2)

        if self.end_date_ >= self.election_day_ and self.cd_.HasField('results'):
            dem_evs = self.cd_.results.evs.dem
            gop_evs = self.cd_.results.evs.gop
            ax.plot([self.election_day_], [dem_evs], marker = '*', markersize = 10, color = 'blue',
                    clip_on = False, zorder = 4)
            ax.plot([self.election_day_], [gop_evs], marker = '*', markersize = 10, color = 'red',
                    clip_on = False, zorder = 4)

        ax.set_title("{} {} Race History".format(self.cd_.election_day.year, self.race_title))
        ax.set_ylabel(ScoreGrapher.kYAxisLabels[self.race_type_])

        # Must be after setting bounds/limits.
        self.config(fig, ax)
        return fig

    @property
    def type(self):
        return 'score'

    kScoreAccessors = {
        'president': get_pres_score,
        'senate': lambda model: (model.senate_median, model.dem_senate_range),
        'house': lambda model: (model.house_median, model.dem_house_range),
    }
    kYAxisLabels = {
        'president': 'Electoral Votes',
        'senate': 'Seats',
        'house': 'Seats',
    }

if __name__ == '__main__':
    main()
