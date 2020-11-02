<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
 <meta http-equiv="content-type" content="text/html; charset=UTF-8">
 <meta http-equiv="content-language" content="en">
 <title>{{ year }} Election Vote {{ race_type }} Share Graphs</title>
 <link rel="stylesheet" title="Default Stylesheet" type="text/css" href="style.css">
 <script src="functions.js"></script>
</head>
<body>
<h2>{{ year }} Election {{ race_type }} Vote Share Graphs</h2>

{% for entry in entries %}
<div>
 {{ entry.region }} {{ race_type }} - {{ entry.dem_candidate }} vs. {{ entry.gop_candidate }}
 <br/>
 <img src="{{ entry.graph_image }}"/>
 <hr>
</div>
{% endfor %}

</body>
</html>
