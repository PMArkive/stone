<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
 <meta http-equiv="content-type" content="text/html; charset=UTF-8">
 <meta http-equiv="content-language" content="en">
 <title>{{ year }} Election Wrongometer</title>
 <link rel="stylesheet" title="Default Stylesheet" type="text/css" href="style.css">
 <link rel="stylesheet" title="Default Stylesheet" type="text/css" href="wrongometer.css">
 <script src="functions.js"></script>
 <script src="meter.js"></script>
 <script type="text/javascript">
  window.addEventListener("load", function () {
    AddRangeTicks(document.getElementById("tickbox"));
    document.getElementById("slider").addEventListener("input", function (event) {
      return OnSliderChange(event);
    });
  }, false);
 </script>
</head>
<body>

<p>
<b>Slide the scale to see how the electoral map looks if polls are uniformly off. Error between 0-6 points is not unusual.</b>
<br/>
<br/>
Error: <span id="selected_error">None</span>
</p>

<div class="slider_div">
 <input id="slider" type="range" min="-20" max="20" step="1" size="80"/>
 <div class="tickbox" id="tickbox">
 </div>
</div>

<section>
 <div class="column">
  <table>
   <thead>
    <tr>
     <th colspan="2">Polling Averages</th>
     <th colspan="2">Tipping Points</th>
    </tr>
    <tr>
     <th colspan="1">States</th>
     <th colspan="1">Margin</th>
     <th colspan="1">D EVs</th>
     <th colspan="1">R EVs</th>
    </tr>
   </thead>
   <tbody>
   {% for state in states %}
   <tr id="state_{{ state.id }}" class="{{ state.class }}" data-margin="{{ state.raw_margin }}" data-evs="{{ state.evs }}" data-code="{{ state.code }}">
    <td><b>{{ state.name }}</b></td>
    <td><b class="{{ state.margin_class }}" id="state_{{ state.id }}_margin">{{ state.margin_text }}</b></td>
    <td><b class="dem">{{ state.dem_ev }}</b></td>
    <td><b class="gop">{{ state.gop_ev }}</b></td>
   </tr>
   {% endfor %}
   </tbody>
  </table>
 </div>
 <div class="column">
  <h2 class="dem">{{ dem_pres }}: <span id="dem_evs">{{ dem_evs }}</span></h2>
  <h2 class="gop">{{ gop_pres }}: <span id="gop_evs">{{ gop_evs }}</span></h2>
  <h2 class="tie">Ties: <span id="tie_evs" data-total="{{ total_evs }}">{{ tie_evs }}</span></h2>
  {{ map_svg }}
 </div>
</section>

</body>
</html>
