{% if length(prev_url) %}
[ <a href="{{ prev_url }}">Prev</a> ]
{% else %}
[ Prev ]
{% endif %}
&nbsp;
{% if length(next_url) %}
{% if next_is_final_results %}
[ <a href="{{ next_url }}">FINAL RESULTS</a> ]
{% else %}
[ <a href="{{ next_url }}">Next</a> ]
{% endif %}
{% else %}
[ Next ]
{% endif %}
