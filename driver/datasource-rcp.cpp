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

#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

#include <google/protobuf/text_format.h>
#include <PicoSHA2/picosha2.h>
#include "campaign.h"
#include "context.h"
#include "datasource-rcp.h"
#include "logging.h"
#include "progress-bar.h"
#include "utility.h"

namespace stone {

using namespace std::string_literals;

static std::string kNational2012Url =
    "https://www.realclearpolitics.com/epolls/2012/president/us/general_election_romney_vs_obama-1171.html";
static std::string kGenericBallot2012Url =
    "https://www.realclearpolitics.com/epolls/other/2012_generic_congressional_vote-3525.html";

static const std::vector<std::string> kPres2012Urls = {
    "https://www.realclearpolitics.com/epolls/2012/president/az/arizona_romney_vs_obama-1757.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ar/arkansas_romney_vs_obama-2918.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ca/california_romney_vs_obama-2009.html",
    "https://www.realclearpolitics.com/epolls/2012/president/co/colorado_romney_vs_obama-2023.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ct/connecticut_romney_vs_obama-2906.html",
    "https://www.realclearpolitics.com/epolls/2012/president/fl/florida_romney_vs_obama-1883.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ga/georgia_romney_vs_obama-2150.html",
    "https://www.realclearpolitics.com/epolls/2012/president/hi/hawaii_romney_vs_obama-2954.html",
    "https://www.realclearpolitics.com/epolls/2012/president/id/idaho_romney_vs_obama-3388.html",
    "https://www.realclearpolitics.com/epolls/2012/president/il/illinois_romney_vs_obama-2955.html",
    "https://www.realclearpolitics.com/epolls/2012/president/in/indiana_romney_vs_obama-3167.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ia/iowa_romney_vs_obama-1922.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ks/kansas_romney_vs_obama-2988.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ky/kentucky_romney_vs_obama-2889.html",
    "https://www.realclearpolitics.com/epolls/2012/president/la/louisiana_romney_vs_obama-2942.html",
    "https://www.realclearpolitics.com/epolls/2012/president/me/maine_romney_vs_obama-2097.html",
    "https://www.realclearpolitics.com/epolls/2012/president/md/maryland_romney_vs_obama-3218.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ma/massachusetts_romney_vs_obama-1804.html",
    "https://www.realclearpolitics.com/epolls/2012/president/mi/michigan_romney_vs_obama-1811.html",
    "https://www.realclearpolitics.com/epolls/2012/president/mn/minnesota_romney_vs_obama-1823.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ms/mississippi_romney_vs_obama-2122.html",
    "https://www.realclearpolitics.com/epolls/2012/president/mo/missouri_romney_vs_obama-1800.html",
    "https://www.realclearpolitics.com/epolls/2012/president/mt/montana_romney_vs_obama-1780.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ne/nebraska_romney_vs_obama-1976.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nv/nevada_romney_vs_obama-1908.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nh/new_hampshire_romney_vs_obama-2030.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nj/new_jersey_romney_vs_obama-1912.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nm/new_mexico_romney_vs_obama-2027.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ny/new_york_romney_vs_obama-2868.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nc/north_carolina_romney_vs_obama-1784.html",
    "https://www.realclearpolitics.com/epolls/2012/president/nd/north_dakota_romney_vs_obama-3238.html",
    "https://www.realclearpolitics.com/epolls/2012/president/oh/ohio_romney_vs_obama-1860.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ok/oklahoma_romney_vs_obama-3215.html",
    "https://www.realclearpolitics.com/epolls/2012/president/or/oregon_romney_vs_obama-2749.html",
    "https://www.realclearpolitics.com/epolls/2012/president/pa/pennsylvania_romney_vs_obama-1891.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ri/rhode_island_romney_vs_obama-2072.html",
    "https://www.realclearpolitics.com/epolls/2012/president/sc/south_carolina_romney_vs_obama-1999.html",
    "https://www.realclearpolitics.com/epolls/2012/president/sd/south_dakota_romney_vs_obama-1980.html",
    "https://www.realclearpolitics.com/epolls/2012/president/tn/tennessee_romney_vs_obama-2047.html",
    "https://www.realclearpolitics.com/epolls/2012/president/tx/texas_romney_vs_obama-1945.html",
    "https://www.realclearpolitics.com/epolls/2012/president/ut/utah_romney_vs_obama-1763.html",
    "https://www.realclearpolitics.com/epolls/2012/president/vt/vermont_romney_vs_obama-2880.html",
    "https://www.realclearpolitics.com/epolls/2012/president/va/virginia_romney_vs_obama-1774.html",
    "https://www.realclearpolitics.com/epolls/2012/president/wa/washington_romney_vs_obama-2708.html",
    "https://www.realclearpolitics.com/epolls/2012/president/wv/west_virginia_romney_vs_obama-1970.html",
    "https://www.realclearpolitics.com/epolls/2012/president/wi/wisconsin_romney_vs_obama-1871.html",
};

static const std::vector<std::string> kSenate2012Urls = {
    "https://www.realclearpolitics.com/epolls/2012/senate/az/arizona_senate_flake_vs_carmona-3005.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ca/california_senate_emken_vs_feinstein-3220.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ct/connecticut_senate_mcmahon_vs_murphy-2111.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/de/delaware_senate_wade_vs_carper-3352.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/fl/florida_senate_mack_vs_nelson-1847.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/hi/hawaii_senate_lingle_vs_hirono-2138.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/in/indiana_senate_mourdock_vs_donnelly-3166.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ma/massachusetts_senate_brown_vs_warren-2093.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/md/maryland_senate_bongino_vs_cardin-3236.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/mi/michigan_senate_hoekstra_vs_stabenow-1817.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/mn/minnesota_senate_bills_vs_klobuchar-3230.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/mo/missouri_senate_akin_vs_mccaskill-2079.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ms/mississippi_senate_wicker_vs_gore-3234.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/mt/montana_senate_rehberg_vs_tester-1826.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/nd/north_dakota_senate_berg_vs_heitkamp-3212.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ne/nebraska_senate_fischer_vs_kerrey-3144.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/nj/new_jersey_senate_kyrillos_vs_menendez-1924.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/nm/new_mexico_senate_wilson_vs_heinrich-2016.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/nv/nevada_senate_heller_vs_berkley-1894.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ny/new_york_senate_long_vs_gillibrand-3162.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/oh/ohio_senate_mandel_vs_brown-2100.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/pa/pennsylvania_senate_smith_vs_casey-3008.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ri/rhode_island_senate_hinckley_vs_whitehouse-3205.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/tn/tennessee_senate_corker_vs_clayton-3333.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/tx/texas_senate_cruz_vs_sadler-3074.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/ut/utah_senate_hatch_vs_howell-3235.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/va/virginia_senate_allen_vs_kaine-1833.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/vt/vermont_senate_macgovern_vs_sanders-3394.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/wa/washington_senate_baumgartner_vs_cantwell-3012.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/wi/wisconsin_senate_thompson_vs_baldwin-2711.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/wv/west_virginia_senate_raese_vs_manchin-1950.html",
    "https://www.realclearpolitics.com/epolls/2012/senate/wy/wyoming_senate_barrasso_vs_chesnut-3380.html",
};

static const std::string kGenericBallot2014Url =
    "https://www.realclearpolitics.com/epolls/other/generic_congressional_vote-2170.html";

static const std::string kGenericBallot2016Url =
    "https://www.realclearpolitics.com/epolls/other/2016_generic_congressional_vote-5279.html";

static const std::vector<std::string> kGov2016Urls = {
    "https://www.realclearpolitics.com/epolls/2016/governor/in/indiana_governor_holcomb_vs_gregg-6097.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/mo/missouri_governor_greitens_vs_koster-5627.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/mt/montana_governor_gianforte_vs_bullock-6098.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/nh/new_hampshire_governor_sununu_vs_van_ostern-5411.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/nc/north_carolina_governor_mccrory_vs_cooper-4096.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/or/oregon_governor_pierce_vs_brown-6128.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/ut/utah_governor_herbert_vs_weinholtz-6090.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/vt/vermont_governor_scott_vs_minter-6101.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/wa/washington_governor_bryant_vs_inslee-5761.html",
    "https://www.realclearpolitics.com/epolls/2016/governor/wv/west_virginia_governor_cole_vs_justice-5890.html",
};

static std::vector<std::string> kSenate2014Urls = {
    "https://www.realclearpolitics.com/epolls/2014/senate/ak/alaska_senate_sullivan_vs_begich-3658.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/al/alabama_senate-4350.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ar/arkansas_senate_cotton_vs_pryor-4049.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/co/colorado_senate_gardner_vs_udall-3845.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/de/delaware_senate_wade_vs_coons-5218.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ga/georgia_senate_perdue_vs_nunn_vs_swafford-5253.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/hi/hawaii_senate_cavasso_vs_schatz-5148.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ia/iowa_senate_ernst_vs_braley-3990.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/id/idaho_senate_risch_vs_mitchell-5060.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/il/illinois_senate_oberweis_vs_durbin-4228.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ks/kansas_senate_roberts_vs_orman-5216.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ky/kentucky_senate_mcconnell_vs_grimes-3485.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/la/louisiana_senate_cassidy_vs_landrieu-3670.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ma/massachusetts_senate_herr_vs_markey-5151.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/me/maine_senate_collins_vs_bellows-4168.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/mi/michigan_senate_land_vs_peters-3820.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/mn/minnesota_senate_mcfadden_vs_franken-3902.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ms/mississippi_senate_cochran_vs_childers-4179.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/mt/montana_senate_daines_vs_curtis-5190.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/nc/north_carolina_senate_tillis_vs_hagan_vs_haugh-5136.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ne/nebraska_senate_sasse_vs_domina-5004.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/nh/new_hampshire_senate_brown_vs_shaheen-3894.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/nj/new_jersey_senate_bell_vs_booker-5092.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/nm/new_mexico_senate_weh_vs_udall-4990.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ok/oklahoma_senate_lankford_vs_johnson-5152.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/or/oregon_senate_wheby_vs_merkley-5001.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/ri/rhode_island_senate_zaccaria_vs_reed-5201.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/sc/south_carolina_senate_graham_vs_hutto-5101.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/sd/south_dakota_senate_rounds_vs_weiland-4091.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/tn/tennessee_senate_alexander_vs_ball-5032.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/tx/texas_senate_cornyn_vs_alameel-5011.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/va/virginia_senate_gillespie_vs_warner-4255.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/wv/west_virginia_senate_capito_vs_tennant-4088.html",
    "https://www.realclearpolitics.com/epolls/2014/senate/wy/wyoming_senate_enzi_vs_hardy-5154.html",
};

static std::string kGenericBallot2010Url =
    "https://www.realclearpolitics.com/epolls/other/2010_generic_congressional_vote-2171.html";

static std::vector<std::string> kSenate2010Urls = {
    "https://www.realclearpolitics.com/epolls/2010/senate/ak/alaska_senate_miller_vs_mcadams_vs_murkowski-1700.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/al/alabama_senate_shelby_vs_barnes-1430.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ar/arkansas_senate_boozman_vs_lincoln-1186.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/az/arizona_senate_mccain_vs_glassman-1433.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ca/california_senate_boxer_vs_fiorina-1094.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/co/colorado_senate_buck_vs_bennet-1106.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ct/connecticut_senate_mcmahon_vs_blumenthal-1145.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/de/delaware_senate_oadonnell_vs_coons-1670.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/fl/florida_senate_rubio_vs_meek_vs_crist-1456.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ga/georgia_senate_isakson_vs_thurmond-1477.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/hi/hawaii_senate_cavasso_vs_inouye-1726.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ia/iowa_senate_grassley_vs_conlin-1217.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/id/idaho_senate_crapo_vs_sullivan-1525.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/il/illinois_senate_giannoulias_vs_kirk-1092.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/in/indiana_senate_coats_vs_ellsworth-1209.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ks/kansas_senate_moran_vs_johnston-1520.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ky/kentucky_senate_paul_vs_conway-1148.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/la/louisiana_senate_vitter_vs_melancon-1095.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/md/maryland_senate_wargotz_vs_mikulski-1665.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/mo/missouri_senate_blunt_vs_carnahan-1066.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/nc/north_carolina_senate_burr_vs_marshall-1111.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/nd/north_dakota_senate_hoeven_vs_potter-1419.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/nh/new_hampshire_senate_ayotte_vs_hodes-1093.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/nv/nevada_senate_angle_vs_reid-1517.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ny/new_york_senate_dioguardi_vs_gillibrand-1469.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ny/new_york_senate_townsend_vs_schumer-1506.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/oh/ohio_senate_portman_vs_fisher-1069.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ok/oklahoma_senate_coburn_vs_rogers-1648.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/or/oregon_senate_huffman_vs_wyden-1207.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/pa/pennsylvania_senate_sestak_vs_toomey-1059.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/sc/south_carolina_senate_demint_vs_greene-1612.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/sd/south_dakota_senate-1730.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/ut/utah_senate_lee_vs_granato-1626.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/vt/vermont_senate_britton_vs_leahy-1625.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/wa/washington_senate_rossi_vs_murray-1183.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/wi/wisconsin_senate_feingold_vs_johnson-1577.html",
    "https://www.realclearpolitics.com/epolls/2010/senate/wv/west_virginia_senate_special_election_raese_vs_manchin-1673.html",
};

static std::string kNational2008Url =
    "https://www.realclearpolitics.com/epolls/2008/president/us/general_election_mccain_vs_obama-225.html";
static std::string kGenericBallot2008Url =
    "https://www.realclearpolitics.com/epolls/other/2008_generic_congressional_vote-2173.html";

static std::vector<std::string> kPres2008Urls = {
    "https://www.realclearpolitics.com/epolls/2008/president/al/alabama_mccain_vs_obama-557.html",
    "https://www.realclearpolitics.com/epolls/2008/president/hi/hawaii_mccain_vs_obama-598.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ma/massachusetts_mccain_vs_obama-575.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nm/new_mexico_mccain_vs_obama-448.html",
    "https://www.realclearpolitics.com/epolls/2008/president/sd/south_dakota_mccain_vs_obama-626.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ak/alaska_mccain_vs_obama-640.html",
    "https://www.realclearpolitics.com/epolls/2008/president/id/idaho_mccain_vs_obama-600.html",
    "https://www.realclearpolitics.com/epolls/2008/president/mi/michigan_mccain_vs_obama-553.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ny/new_york_mccain_vs_obama-343.html",
    "https://www.realclearpolitics.com/epolls/2008/president/tn/tennessee_mccain_vs_obama-572.html",
    "https://www.realclearpolitics.com/epolls/2008/president/az/arizona_mccain_vs_obama-570.html",
    "https://www.realclearpolitics.com/epolls/2008/president/il/illinois_mccain_vs_obama-602.html",
    "https://www.realclearpolitics.com/epolls/2008/president/mn/minnesota_mccain_vs_obama-550.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nc/north_carolina_mccain_vs_obama-334.html",
    "https://www.realclearpolitics.com/epolls/2008/president/tx/texas_mccain_vs_obama-628.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ar/arkansas_mccain_vs_obama-592.html",
    "https://www.realclearpolitics.com/epolls/2008/president/in/indiana_mccain_vs_obama-604.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ms/mississippi_mccain_vs_obama-612.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nd/north_dakota_mccain_vs_obama-618.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ut/utah_mccain_vs_obama-635.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ca/california_mccain_vs_obama-558.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ia/iowa_mccain_vs_obama-209.html",
    "https://www.realclearpolitics.com/epolls/2008/president/mo/missouri_mccain_vs_obama-545.html",
    "https://www.realclearpolitics.com/epolls/2008/president/oh/ohio_mccain_vs_obama-400.html",
    "https://www.realclearpolitics.com/epolls/2008/president/va/virginia_mccain_vs_obama-551.html",
    "https://www.realclearpolitics.com/epolls/2008/president/co/colorado_mccain_vs_obama-546.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ks/kansas_mccain_vs_obama-555.html",
    "https://www.realclearpolitics.com/epolls/2008/president/mt/montana_mccain_vs_obama-614.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ok/oklahoma_mccain_vs_obama-620.html",
    "https://www.realclearpolitics.com/epolls/2008/president/vt/vermont_mccain_vs_obama-630.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ct/connecticut_mccain_vs_obama-527.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ky/kentucky_mccain_vs_obama-578.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ne/nebraska_mccain_vs_obama-616.html",
    "https://www.realclearpolitics.com/epolls/2008/president/or/oregon_mccain_vs_obama-548.html",
    "https://www.realclearpolitics.com/epolls/2008/president/wa/washington_mccain_vs_obama-576.html",
    "https://www.realclearpolitics.com/epolls/2008/president/de/delaware_mccain_vs_obama-594.html",
    "https://www.realclearpolitics.com/epolls/2008/president/la/louisiana_mccain_vs_obama-606.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nv/nevada_mccain_vs_obama-252.html",
    "https://www.realclearpolitics.com/epolls/2008/president/pa/pennsylvania_mccain_vs_obama-244.html",
    "https://www.realclearpolitics.com/epolls/2008/president/wv/west_virginia_mccain_vs_obama-632.html",
    "https://www.realclearpolitics.com/epolls/2008/president/fl/florida_mccain_vs_obama-418.html",
    "https://www.realclearpolitics.com/epolls/2008/president/me/maine_mccain_vs_obama-608.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nh/new_hampshire_mccain_vs_obama-195.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ri/rhode_island_mccain_vs_obama-622.html",
    "https://www.realclearpolitics.com/epolls/2008/president/wi/wisconsin_mccain_vs_obama-549.html",
    "https://www.realclearpolitics.com/epolls/2008/president/ga/georgia_mccain_vs_obama-596.html",
    "https://www.realclearpolitics.com/epolls/2008/president/md/maryland_mccain_vs_obama-610.html",
    "https://www.realclearpolitics.com/epolls/2008/president/nj/new_jersey_mccain_vs_obama-250.html",
    "https://www.realclearpolitics.com/epolls/2008/president/sc/south_carolina_mccain_vs_obama-624.html",
    "https://www.realclearpolitics.com/epolls/2008/president/wy/wyoming_mccain_vs_obama-634.html",
};

static std::vector<std::string> kSenate2008Urls = {
    "https://www.realclearpolitics.com/epolls/2008/senate/ak/alaska_senate-562.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/al/alabama_senate-930.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ar/arkansas_senate-913.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/co/colorado_senate-556.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/de/delaware_senate-914.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ga/georgia_senate-302.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ia/iowa_senate-929.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/id/idaho_senate-931.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/il/illinois_senate-915.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ks/kansas_senate-916.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ky/kentucky_senate-917.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/la/louisiana_senate-565.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ma/massachusetts_senate-918.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/me/maine_senate-564.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/mi/michigan_senate-908.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/mn/minnesota_senate-257.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ms/mississippi_senate-912.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ms/mississippi_senate_special-911.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/mt/montana_senate-919.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/nc/north_carolina_senate-910.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ne/nebraska_senate-920.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/nh/new_hampshire_senate-354.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/nj/new_jersey_senate-585.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/nm/new_mexico_senate-561.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ok/oklahoma_senate-921.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/or/oregon_senate-566.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/ri/rhode_island_senate-922.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/sc/south_carolina_senate-923.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/sd/south_dakota_senate-924.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/tn/tennessee_senate-925.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/tx/texas_senate-909.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/va/virginia_senate-537.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/wv/west_virginia_senate-926.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/wy/wyoming_senate-927.html",
    "https://www.realclearpolitics.com/epolls/2008/senate/wy/wyoming_senate_special-928.html",
};

static std::string kGenericBallot2006Url =
    "https://www.realclearpolitics.com/epolls/other/2006_generic_congressional_vote-2174.html";

static std::vector<std::string> kSenate2006Urls = {
    "https://www.realclearpolitics.com/epolls/2006/senate/mo/missouri_senate_race-12.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/mt/montana_senate_race-11.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/va/virginia_senate_race-14.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/md/maryland_senate_race-114.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/ri/rhode_island_senate_race-17.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/nj/new_jersey_senate_race-10.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/az/arizona_senate_race-35.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/tn/tennessee_senate_race-20.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/mn/minnesota_senate_race-15.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/mi/michigan_senate_race-24.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/oh/ohio_senate_race-2.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/pa/pennsylvania_senate_race-1.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/wa/washington_senate_race-9.html",
    "https://www.realclearpolitics.com/epolls/2006/senate/ct/connecticut_senate_race-21.html",
};

static std::string kNational2004Url =
    "https://www.realclearpolitics.com/epolls/2004/president/us/general_election_bush_vs_kerry-939.html";
static std::string kGenericBallot2004Url =
    "https://www.realclearpolitics.com/epolls/other/2004_generic_congressional_vote-2175.html";

static inline std::string
CreatePollId(const Poll& poll)
{
    picosha2::hash256_one_by_one hasher;

    std::ostringstream out;
    out << poll.description() << "*" << poll.start() << "*" << poll.end() << "*" << poll.dem()
        << "*" << poll.gop() << "*" << poll.margin() << "*" << poll.url() << "*"
        << poll.sample_size() << "*" << poll.sample_type() << "*" << poll.published();
    auto bytes = out.str();

    hasher.process(bytes.begin(), bytes.end());
    hasher.finish();

    return picosha2::get_hash_hex_string(hasher);
}

static std::optional<Feed>
FetchArchived(Context* cx, Campaign* cc, const std::string& national_url,
              const std::string& generic_ballot_url, const std::vector<std::string> state_urls,
	      const std::vector<std::string> senate_urls)
{
    Feed feed;
    feed.mutable_info()->set_description("RealClearPolitics");
    feed.mutable_info()->set_short_name("rcp");
    feed.mutable_info()->set_feed_type("normal");

    using PollSource = std::tuple<Race_RaceType, int, std::string>;

    std::vector<PollSource> sources;
    for (const auto& state : cc->state_list()) {
        auto hp_name = state.name();
        std::transform(hp_name.begin(), hp_name.end(), hp_name.begin(), ::tolower);
        hp_name = std::regex_replace(hp_name, std::regex(" "), "_");

        std::string url;
        for (const auto& other : state_urls) {
            if (other.find(hp_name) != std::string::npos) {
                url = other;
                break;
            }
        }
        if (url.empty())
            continue;
        sources.emplace_back(Race::ELECTORAL_COLLEGE, state.race_id(), url);
    }

    for (const auto& race : cc->senate_map().races()) {
        auto iter = kStateCodes.find(race.region());
        if (iter == kStateCodes.end()) {
            Err() << "State not recognized: " << race.region();
            continue;
        }

        std::string lcase_code = iter->second;
        std::transform(lcase_code.begin(), lcase_code.end(), lcase_code.begin(), ::tolower);

        std::string url;
        std::string part = "senate/" + lcase_code + "/";
        for (const auto& candidate_url : senate_urls) {
            if (candidate_url.find(part) != std::string::npos) {
                url = candidate_url;
                break;
            }
        }
        if (url.empty())
            continue;
        sources.emplace_back(Race::SENATE, race.race_id(), url);
    }

    if (!national_url.empty())
        sources.emplace_back(Race::NATIONAL, -1, national_url);
    sources.emplace_back(Race::GENERIC_BALLOT, -1, generic_ballot_url);

    ProgressBar pbar("Processing feeds", sources.size());
    for (const auto& source : sources) {
        const auto& race_type = std::get<0>(source);
        const auto& race_id = std::get<1>(source);
        const auto& url = std::get<2>(source);

        auto thread_fn = [&feed, cx, cc, race_type, race_id, url](ThreadPool* pool) -> void {
            auto data = cx->Download(url, false);
            if (data.empty()) {
                Err() << "Could not download: " << url << "";
                return;
            }

            std::string output;
            std::vector<std::string> argv = {
                GetExecutableDir() + "/" + "dump-rcp-2012",
                "--year", std::to_string(cc->EndDate().year()),
            };
            if (!Run(argv, data, &output)) {
                Err() << "Could not process: " << url;
                return;
            }

            PollList polls;
            if (!google::protobuf::TextFormat::ParseFromString(output, &polls)) {
                Err() << "Could not parse proto";
                return;
            }

            for (auto& poll : *polls.mutable_polls())
                poll.set_id(CreatePollId(poll));

            auto done = [&feed, cc, race_type, race_id, polls{std::move(polls)}]() -> void {
                if (race_type == Race::NATIONAL) {
                    *feed.mutable_national_polls() = polls.polls();
                } else if (race_type == Race::GENERIC_BALLOT) {
                    *feed.mutable_generic_ballot_polls() = polls.polls();
                } else if (race_type == Race::ELECTORAL_COLLEGE) {
                    auto state_name = cc->state_list()[race_id].name();
                    feed.mutable_states()->insert({state_name, std::move(polls)});
                } else {
                    assert(race_type == Race::SENATE);
                    feed.mutable_senate_polls()->insert({race_id, std::move(polls)});
                }
            };
            pool->OnComplete(std::move(done));
        };

        auto complete = [&pbar]() -> void {
            pbar.Increment();
        };
        cx->workers().Do(std::move(thread_fn), std::move(complete));
    }

    cx->workers().RunCompletionTasks();
    pbar.Finish();

    return {std::move(feed)};
}


static std::optional<Feed>
Fetch2004(Context* cx, Campaign* cc)
{
    Feed feed;
    feed.mutable_info()->set_description("RealClearPolitics");
    feed.mutable_info()->set_short_name("rcp");
    feed.mutable_info()->set_feed_type("normal");

    std::vector<std::pair<std::string, std::string>> urls;
    urls.emplace_back("National", kNational2004Url);
    urls.emplace_back("Generic Ballot", kGenericBallot2004Url);

    for (const auto& [state_name, code] : kStateCodes) {
        if (code.size() > 2 || code == "DC")
            continue;

        std::string lcase_code = code;
        std::transform(lcase_code.begin(), lcase_code.end(), lcase_code.begin(), ::tolower);

        std::string url = "https://www.realclearpolitics.com/Presidential_04/" + lcase_code +
                          "_polls.html";
        urls.emplace_back(state_name, url);
    }

    ProgressBar pbar("Processing feeds", urls.size());
    for (const auto& pair : urls) {
        const auto& state_name = pair.first;
        const auto& url = pair.second;

        auto thread_fn = [&feed, cx, cc, state_name, url](ThreadPool* pool) -> void {
            auto data = cx->Download(url, false);
            if (data.empty()) {
                Err() << "Could not download: " << url;
                return;
            }

            std::string format;
            if (state_name == "National" || state_name == "Generic Ballot")
                format = "new";
            else
                format = "old";

            std::string output;
            std::vector<std::string> argv = {
                GetExecutableDir() + "/dump-rcp-2012",
                "--year", std::to_string(cc->EndDate().year()),
                "--format", format,
            };
            if (!Run(argv, data, &output)) {
                Err() << "Could not process: " << url;
                return;
            }

            PollList polls;
            if (!google::protobuf::TextFormat::ParseFromString(output, &polls)) {
                Err() << "Could not parse proto";
                return;
            }

            for (auto& poll : *polls.mutable_polls())
                poll.set_id(CreatePollId(poll));

            auto done = [&feed, state_name, polls{std::move(polls)}]() mutable -> void {
                if (state_name == "National")
                    *feed.mutable_national_polls() = std::move(*polls.mutable_polls());
                else if (state_name == "Generic Ballot")
                    *feed.mutable_generic_ballot_polls() = std::move(*polls.mutable_polls());
                else
                    feed.mutable_states()->insert({state_name, std::move(polls)});
            };
            pool->OnComplete(std::move(done));
        };

        auto complete = [&pbar]() -> void {
            pbar.Increment();
        };
        cx->workers().Do(std::move(thread_fn), std::move(complete));
    }

    cx->workers().RunCompletionTasks();
    pbar.Finish();

    return {std::move(feed)};
}

std::optional<Feed>
DataSourceRcp::Fetch(Context* cx, Campaign* cc)
{
    switch (cc->EndDate().year()) {
        case 2014:
            return FetchArchived(cx, cc, {}, kGenericBallot2014Url, {}, kSenate2014Urls);
        case 2012:
            return FetchArchived(cx, cc, kNational2012Url, kGenericBallot2012Url, kPres2012Urls,
                                 kSenate2012Urls);
        case 2010:
            return FetchArchived(cx, cc, {}, kGenericBallot2010Url, {}, kSenate2010Urls);
        case 2008:
            return FetchArchived(cx, cc, kNational2008Url, kGenericBallot2008Url, kPres2008Urls,
                                 kSenate2008Urls);
        case 2006:
            return FetchArchived(cx, cc, {}, kGenericBallot2006Url, {}, kSenate2006Urls);
        case 2004:
            return Fetch2004(cx, cc);
    }
    return {};
}

static std::string
GetRaceUrlByStateCode(const std::vector<std::string>& urls, const Race& race,
                      const std::string& prefix, const std::string& suffix)
{
    auto iter = kStateCodes.find(race.region());
    if (iter == kStateCodes.end()) {
        Err() << "Region not recognized: " << race.region();
        return {};
    }

    std::string lcase_code = iter->second;
    std::transform(lcase_code.begin(), lcase_code.end(), lcase_code.begin(), ::tolower);
    std::string part = prefix + lcase_code + suffix;
    for (const auto& url : urls) {
        if (url.find(part) != std::string::npos)
            return url;
    }
    return {};
}

std::optional<ProtoPollMap>
DataSourceRcp::FetchGovernors(Context* cx, Campaign* cc, int year)
{
    const std::vector<std::string>* urls_ptr = nullptr;
    switch (year) {
        case 2016:
            urls_ptr = &kGov2016Urls;
            break;
        default:
            Err() << "RCP: No governor URL list for " << year;
            return {};
    }

    const std::vector<std::string>& urls = *urls_ptr;
    const auto& governor_map = cc->governor_map();

    ProtoPollMap master;

    ProgressBar pbar("Processing feeds", governor_map.races().size());
    for (const auto& race : governor_map.races()) {
        AutoIncrement auto_inc(&pbar);

        auto url = GetRaceUrlByStateCode(urls, race, "/", "/");
        if (url.empty())
            continue;

        auto_inc.Cancel();

        auto thread_fn = [cx, url, year, &race, &master](ThreadPool* pool) -> void {
            auto data = cx->Download(url, false);
            if (data.empty()) {
                Err() << "Could not download: " << url;
                return;
            }

            std::string output;
            std::vector<std::string> argv = {
                GetExecutableDir() + "/" + "dump-rcp-2012",
                "--year", std::to_string(year),
                "--format", "new",
            };
            if (!Run(argv, data, &output))
                return;

            PollList list;
            if (!google::protobuf::TextFormat::ParseFromString(output, &list)) {
                Err() << "Could not parse proto";
                return;
            }

            for (auto& poll : *list.mutable_polls()) {
                poll.mutable_start()->set_year(year);
                poll.mutable_end()->set_year(year);
                poll.set_id(CreatePollId(poll));
            }

            auto done = [&master, &race, list{std::move(list)}]() -> void {
                master[race.race_id()] = std::move(list);
            };
            pool->OnComplete(std::move(done));
        };

        auto complete = [&pbar]() -> void {
            pbar.Increment();
        };
        cx->workers().Do(std::move(thread_fn), std::move(complete));
    }

    cx->workers().RunCompletionTasks();
    pbar.Finish();

    return {master};
}

std::optional<ProtoPollList>
DataSourceRcp::FetchGenericBallot(Context* cx, Campaign* cc, int year)
{
    std::string url;
    switch (year) {
        case 2016:
            url = kGenericBallot2016Url;
            break;
        default:
            Err() << "No RCP URL for " << year << " generic ballot";
            return {};
    }

    auto data = cx->Download(url, true);
    if (data.empty()) {
        Err() << "Could not download: " << url;
        return {};
    }

    std::string output;
    std::vector<std::string> argv = {
        GetExecutableDir() + "/" + "dump-rcp-2012",
        "--year", std::to_string(year),
        "--format", "new",
    };
    if (!Run(argv, data, &output))
        return {};

    PollList list;
    if (!google::protobuf::TextFormat::ParseFromString(output, &list)) {
        Err() << "Could not parse proto";
        return {};
    }
    for (auto& poll : *list.mutable_polls())
        poll.set_id(CreatePollId(poll));

    return {std::move(*list.mutable_polls())};
}

} // namespace stone
