<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
 <meta http-equiv="content-type" content="text/html; charset=UTF-8">
 <meta http-equiv="content-language" content="en">
 <title>{{ year }} Election Topline Measures</title>
 <link rel="stylesheet" title="Default Stylesheet" type="text/css" href="style.css">
 <script src="functions.js"></script>
</head>
<body>
<h2>{{ year }} Election Topline Measures</h2>

<b><u>Date:</u></b>&nbsp; {{ date }}
<br/></br>

<div class="yellow_box">
  {% if exists("mean_ev") %}
  <b><u>Electoral College</u>:</b> {{ mean_ev }}<br>
  {% endif %}
  {% if exists("mean_senate") %}
  <b><u>Senate</u>:</b> {{ mean_senate }} (<b class="{{ sdt_class }}">{{ sdt_text }})</b><br>
  {% endif %}
  {% if exists("mean_house") %}
  <b><u>House</u>:</b> {{ mean_house }} (<b class="{{ hdt_class }}">{{ hdt_text }})</b><br>
  {% endif %}
  {% if exists("mean_governor") %}
  <b><u>Governors</u>:</b> {{ mean_governor }} (<b class="{{ gdt_class }}">{{ gdt_text }})</b><br>
  {% endif %}
</div>

{% if exists("map_img") %}
<object type="image/svg+xml" data="{{ map_img }}" class="map_image"></object>
{% endif %}

</body>
</html>
