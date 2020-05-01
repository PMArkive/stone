<table>
 <thead>
  <tr>
   {% if race_has_ev %}
   <th colspan="3" style="text-align: center;">Polls</th>
   <th colspan="2">Tipping Points</th>
   {% else %}
   <th colspan="3" style="text-align: center;">Other</th>
   {% endif %}
  </tr>
  <tr>
   <th colspan="1">{{ race_header_text }}</th>
   {% if exists("show_rating") %}
    <th colspan="1">Rating</th>
   {% endif %}
   <th colspan="1">Margin</th>
   {% if is_prediction %}
   <th colspan="1">Change</th>
   {% else %}
    {% if not exists("skip_error") %}
     <th colspan="1">Error</th>
    {% endif %}
   {% endif %}
   {% if race_has_ev %}
   <th colspan="1">D {{ ev_type }}</th>
   <th colspan="1">R {{ ev_type }}</th>
   {% endif %}
  </tr>
 </thead>

 <tbody>
 {% for entry in entries %}
 <tr class="{{ entry.class }}" id="margin_row_{{ entry.code }}">
  <td><span class="xlink" onclick="ExpandPolls(this, 'margin_row_{{ entry.code }}_polls')">&#x27AD;</span>&nbsp;<b>{{ entry.name }}</b></td>
  {% if exists("show_rating") %}
   <td>{% if existsIn(entry, "rating_class") %}<b class="{{ entry.rating_class }}">{{ entry.rating_text }}</b>{% endif %}</td>
  {% endif %}
  <td><b class="{{ entry.margin_class }}">{{ entry.margin_text }}</b></td>
  {% if not exists("skip_error") %}
   <td>{% if existsIn(entry, "dt_class") %}<b class="{{ entry.dt_class }}">{{ entry.dt_value }}</b>{% endif %}</td>
  {% endif %}
  {% if race_has_ev %}
  <td><b class="dem">{{ entry.dem_ev }}</b></td>
  <td><b class="gop">{{ entry.gop_ev }}</b></td>
  {% endif %}
 </tr>
 <tr class="invisible" id="margin_row_{{ entry.code }}_polls">
  <td colspan="5">
   <table class="poll_block">
    <tr>
     <th>Pollster</th>
     <th>Start</th>
     <th>End</th>
     <th>DEM</th>
     <th>GOP</th>
     <th>Margin</th>
    </tr>
    {% for poll in entry.polls %}
    {% if poll.icon != "old" %}
    <tr>
    {% else %}
    <tr class="old_poll_row">
    {% endif %}
     <td class="poll_text_cell">
     {% if poll.icon == "new" %}
       <img src="new-icon.png" width="16" height="16">
     {% else if poll.icon == "old" %}
       Aged Out:&nbsp;
     {% endif %}
     {% if length(poll.url) != 0 %}
       <a class="poll_text_cell" href="{{ poll.url }}">{{ poll.description }}</a></td>
     {% else %}
       {{ poll.description }}
     {% endif %}
     </td>
     <td>{{ poll.start }}</td>
     <td>{{ poll.end }}</td>
     <td>{{ poll.dem }}</td>
     <td>{{ poll.gop }}</td>
     <td><b class="{{ poll.margin_class }}">{{ poll.margin_text }}</b></td>
    </tr>
    {% endfor %}
   </table>
  </td>
 </tr>
 {% endfor %}
 </tbody>
</table>
