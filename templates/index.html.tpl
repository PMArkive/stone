<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
 <meta http-equiv="content-type" content="text/html; charset=UTF-8">
 <meta http-equiv="content-language" content="en">
 <title>{{ year }} Election {% if is_prediction %}Daily Snapshot{% else %}Results{% endif %}</title>
 <link rel="stylesheet" title="Default Stylesheet" type="text/css" href="style.css">
 <script src="functions.js"></script>
</head>
<body>
<h2>{{ year }} Election {% if is_prediction %}Daily Snapshot{% else %}Results{% endif %}</h2>

<b><u>Date:</u></b>&nbsp; {{ date }}&nbsp;{{ nav_text }}{% if backdated %}<i>(Note: Backdated model run)</i>{% endif %}
{% if for_today and not backdated %}
<br><b><u>Last updated:</u></b> {{ generated }}
{% endif %}
<br>
<div id="topline">
  {% if is_presidential_year %}
   <div class="topline_box">
    <b><u>Electoral College</u>:</b><br>
    {% if is_prediction %}
    <b>Prediction:</b> {{ mean_ev }}<br>
    <b>Current Bias:</b> <b class="{{ mm_class }}">{{ mm_text }}</b><br>
    <b>Range:</b> <b class="dem">D {{ dem_ev_low }} to {{ dem_ev_high }}</b><br>
    <b>Rating:</b> {{ dem_ec_win_text}} to win
    {% else %}
    <b>Outcome:</b> {{ actual_ev }}</br>
    <b>Predicted:</b> {{ predicted_ev }}<br>
    <b>Actual Bias:</b> <b class="{{ mm_class }}">{{ mm_text }}</b><br>
    <b>Predicted Bias:</b> <b class="{{ pmm_class }}">{{ pmm_text }}</b>
    {% endif %}
  {% endif %}
   </div>
  {% if has_senate_data == true %}
   <div class="topline_box">
    <b><u>Senate</u>:</b><br>
    {% if no_senate_mm == false %}
     {% if is_prediction %}
      <b>Prediction:</b> {{ mean_senate }} (<b class="{{ sdt_class }}">{{ sdt_text }})</b><br>
      <b>Current Bias:</b> <b class="{{ smm_class }}">{{ smm_text }}</b><br>
      <b>Range:</b> <b class="dem">D {{ dem_senate_low }} to {{ dem_senate_high }}</b> seats<br>
      {% if exists("dem_senate_win_text") %}
        <b>Rating:</b> {{ dem_senate_win_text }}
        {% if exists("dem_senate_half_win_text") %}
          &gt;={{ dem_seats_for_control }} seats; ({{ dem_senate_half_win_text }} &gt;={{ dem_senate_half_seats }} seats)
        {% else %}
          to control
        {% endif %}
      {% endif %}
     {% else %}
      <b>Outcome:</b> {{ actual_senate }} (<b class="{{ asdt_class }}">{{ asdt_text }})</b><br>
      <b>Predicted:</b> {{ mean_senate }} (<b class="{{ sdt_class }}">{{ sdt_text }})</b><br>
      <b>Actual Bias:</b> <b class="{{ smm_class }}">{{ smm_text }}</b><br>
      {% if exists("psmm_text") %}
       <b>Predicted Bias:</b> <b class="{{ psmm_class }}">{{ psmm_text }}</b><br>
      {% endif %}
     {% endif %}
    {% endif %}
   </div>
  {% endif %}
  {% if has_house_data == true %}
   <div class="topline_box">
    <b><u>House</u>:</b><br>
    {% if is_prediction %}
     <b>Prediction:</b> {{ mean_house }} (<b class="{{ hdt_class }}">{{ hdt_text }})</b><br>
     {% if has_house_mm %}
     <b>Current Bias:</b> <b class="{{ hmm_class }}">{{ hmm_text }}</b><br>
     {% endif %}
     <b>Range:</b> <b class="dem">D {{ dem_house_low }} to {{ dem_house_high }}</b> seats
     {% if exists("dem_house_win_text") %}<br><b>Rating:</b> {{ dem_house_win_text }} to control{% endif %}
    {% else %}
     <b>Outcome:</b> {{ actual_house }} (<b class="{{ ahdt_class }}">{{ ahdt_text }})</b><br>
     <b>Predicted:</b> {{ mean_house }} (<b class="{{ hdt_class }}">{{ hdt_text }})</b><br>
     <b>Actual Bias:</b> <b class="{{ hmm_class }}">{{ hmm_text }}</b><br>
     {% if exists("phmm_text") %}
      <b>Predicted Bias:</b> <b class="{{ phmm_class }}">{{ phmm_text }}</b><br>
     {% endif %}
    {% endif %}
   </div>
  {% endif %}
  {% if has_governor_data == true %}
   <div class="topline_box">
    <b><u>Governors:</u></b><br>
    {% if not is_prediction %}
     <b>Outcome:</b> {{ actual_governor }} (<b class="{{ agdt_class }}">{{ agdt_text }})</b><br>
    {% endif %}
    <b>Prediction:</b> {{ mean_governor }} (<b class="{{ gdt_class }}">{{ gdt_text }})</b>
   </div>
  {% endif %}
 </div>
</div>

<div id="data">
 <div class="column">
 <p class="narrow_text_blurb" style="margin-top: -15px;">
 {% if is_presidential_year %}
 The "tipping-point" state, or first state to give a party {{ win_evs }} electoral votes and win the election, is highlighted yellow below. {% if is_prediction %}Click on the arrow next to a race to see the most recent polls.{% endif %}<br>
 The map is colored based on the Presidential polling margins for each state. Down-ballot races are below the map. {% if is_prediction %} Look near the tipping points to decide which races need support.{% else %} The change is the difference between the predicted polling margin and the actual outcome.{% endif %}
 {% else %}
 The "tipping-point" race, or first state to cross a senate majority, is highlighted yellow below. {% if is_prediction %} Click on the arrow next to a race to see the most recent polls.<br>
 Only races with polls are shown. Early in the cycle, competitive races may not yet be polled. We score races for the incumbent or presumed party until data is available.{% else %} The change is the difference between the predicted polling margin and the actual outcome.{% endif %}
 {% endif %}
 </p>
 {% if is_presidential_year %}
 {{ state_table_content }}
 {% else %}
 {{ senate_table_content }}
 {% endif %}
 <br/>
 {{ other_table_content }}
 </div>
 <div class="column">
 {% if is_presidential_year %}
  <object type="image/svg+xml" data="{{ map_img }}" class="map_image"></object>
  {% if no_senate_mm == false %}
  <p>Senate races. Only races with polls are shown. Early in the cycle, competitive races may
     not yet be polled. We score races for the incumbent or presumed party until data is available.</p>
  {{ senate_table_content }}
  {% else %}
   {% if not backdated %}
   <p>There is not yet enough polling data to display senate races.</p>
   {% endif %}
  {% endif %}
 {% endif %}
 {% if has_governor_data == true %}
 <p>Governor races. Only races with polls are shown.</p>
 {{ governor_table_content }}
 {% endif %}
 {% if has_house_data == true %}
 <p>House races. If no polls are available, we use expert ratings, and only show close races.</p>
 {{ house_table_content }}
 {% endif %}
 </div>
</div>

{% if is_presidential_year %}
 <object type="image/svg+xml" data="{{ president_score_img }}" class="graph_image"></object>
 <object type="image/svg+xml" data="{{ president_bias_img }}" class="graph_image"></object>
{% endif %}
<br/>
{% if no_senate_mm == false %}
 <object type="image/svg+xml" data="{{ senate_bias_img }}" class="graph_image"></object>
{% endif %}
{% if has_house_mm and has_house_polls %}
 <object type="image/svg+xml" data="{{ house_bias_img }}" class="graph_image"></object>
{% endif %}
<br/>
{% if is_presidential_year %}
 <object type="image/svg+xml" data="{{ national_bias_img }}" class="graph_image"></object>
{% endif %}
{% if has_generic_ballot %}
 <object type="image/svg+xml" data="{{ generic_ballot_bias_img }}" class="graph_image"></object>
{% endif %}

<div class="text_blurb">
<strong>About</strong>

This tool is based on the work of the Princeton Election Consortium. We snapshot presidential, congressional, and gubernatorial races all in one place. Close races and their polling information are highly visible to help make race assessments. For example, if you are interested in Senate control, you can quickly identify which races are competitive near the tipping point.{% if is_presidential_year == true %} For the Electoral College you can use the tipping point to gauge how secure a candidate's lead is, for example, if you think a candidate might gain or lose 3 points of support by election day.{% endif %}

The headline biases describe how much of an opinion change is needed to bring a race to a tie. For example, an Electoral College bias of D+5 means a 5 point swing is needed across the nation to make the Electoral College a 50/50 "tossup". A Senate bias of R+3 means a 3 opinion swing is needed to bring control of the Senate to a tie. This is not the same as the national popular vote (or the "tipping point" margin), because each type of race (Electoral College, Senate, and House) has uneven distribution of votes:
<ul>
 <li>The Electoral College undervalues or overvalues votes based on geography and political lean.</li>
 <li>The same is true of the House due to gerrymandering.</li>
 <li>The Senate staggers its membership (only 1/3rd is elected each year); it also has the same problems as the EC.</li>
</ul>
<strong>Methodology</strong>

Each state or race's margin is converted into a probability on a normal distribution. Then the likelihoods of all possible Electoral College, Senate, and House outcomes are computed using linear convolution[1]. This process is repeated iteratively, each time simulating small changes in national opinion, until we find the amount of change needed to flip party control [2]. Once this value is found, a Bayesian prediction is made for election day, to estimate the drift in national opinion.

Undecided and third-party voters are used to estimate uncertainty in the predictions. This number can have a big impact, for example: if 10% of voters are undecided, and they split 70-30, a 50-50 race would become 52-48. This moves the margin by four points. In a race with 5% undecided voters, it would only be a 2-point movement. We also estimate uncertainty by using historical variance. For example, 2 months before the election, a 7-point swing has been observed in the Presidential race. This value gets smaller and smaller as election day approaches.

Notes:
<ul>
 <li>The Electoral College prediction is not an average, it's a mode (the most frequently occuring outcome). An average would yield strange maps. For example, if we average a Florida loss (0 votes) and a Florida win (29 votes), we get 14 votes. While 14 votes makes sense statistically, it is not a probable outcome.</li>
 <li>House estimates take the Cook Political Report and UVA ratings, and assign a probability for each rating (tossup = 50%, leans = 70%, likely = 85%, solid = 100%). From this we use the same algorithms as above. In the absence of polls, we convert this rating back to a margin (which winds up being about 4.5 points for "leans", and 10 points for "likely", and use that to compute a bias.</li>
</ul>
<strong>Poll Info</strong>

Polls are taken from RealClearPolitics and FiveThirtyEight (unfortunately HuffPollster is no more). House ratings are from UVA and the Cook Political Report. For shading the map, we consider a polling average of 0-5 to be "leans", 5-10 to be "likely", and 10+ to be "safe". The computed margin is a simple moving average, with the following rules:

<ul>
 <li>Race margins are based on the most recent two weeks of polls for that race. As election time approaches, we narrow that range down to one week.</li>
 <li>If there are less than four recent polls, we'll use older polls. Failing that, we use results from the previous election. This is common early in the election cycle, and for races that are uncompetitive.</li>
 <li>If a race has multiple polls from the same pollster, we average them together as one margin. This is to prevent one pollster from over-influencing the average.</li>
 <li>If a poll is tracking (updates regularly with the same survey panel), we only include the latest version of the poll.</li>
 <li>If a poll is rated as "partisan" by FiveThirtyEight, and has no grade, we exclude it.</li>
 <li>If a poll is graded lower than a "C-" by FiveThirtyEight we exclude it.</li>
</ul>
<strong>Citations</strong>
<ol>
 <li>"A New Approach to Estimating the Probability of Winning the Presidency", Edward Kaplan and Arnold Barnett.</li>
 <li>"Method for analysis of opinion polls in a national electoral system", Sam Wang PhD.</li>
</ol>
</div>

</body>
</html>
