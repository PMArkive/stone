// vim: sts=4 ts=8 sw=4 tw=99 et:
'use strict';

function IsSlimMargin(margin) {
    return Math.abs(margin) < 1.0 && Math.abs(margin) > -1.0;
}

var kDemColors = [
    [10.0, "#0000ff", "dem"],
    [5.0,  "#3399ff", "maybe_dem"],
    [0.0,  "#99ccff", "leans_dem"],
];
var kGopColors = [
    [10.0, "#ff0000", "gop"],
    [5.0,  "#ec7063", "maybe_gop"],
    [0.0,  "#f5b7b1", "leans_gop"],
];

function GetColorForMargin(margin) {
    var colors = (margin > 0) ? kDemColors : kGopColors;
    var abs_margin = Math.abs(margin);
    for (var i = 0; i < colors.length; i++) {
        if (abs_margin >= colors[i][0])
            return [colors[i][1], colors[i][2]];
    }
    return ["#000000", "none"];
}

function OnSliderChange(event) {
    var new_value = event.target.value / 2;

    var new_text;
    if (new_value < 0) {
        new_text = "R+" + Math.abs(new_value);
    } else if (new_value > 0) {
        new_text = "D+" + new_value;
    } else {
        new_text = "None";
    }
    document.getElementById("selected_error").textContent = new_text;

    var dem_ev_elt = document.getElementById("dem_evs");
    var gop_ev_elt = document.getElementById("gop_evs");
    var tie_ev_elt = document.getElementById("tie_evs");

    var total_evs = parseInt(tie_ev_elt.dataset.total);

    var dem_evs = 0;
    var gop_evs = 0;

    var id = 0;
    while (true) {
        var tr = document.getElementById("state_" + id);
        if (tr === null)
            break;

        var new_margin = parseFloat(tr.dataset.margin) + parseFloat(new_value);
        if (new_margin > 99)
            new_margin = 99;
        else if (new_margin < -99)
            new_margin = -99;

        var svg_elt = document.getElementById(tr.dataset.code);

        var box = document.getElementById("state_" + id + "_margin");
        if (!new_margin || IsSlimMargin(new_margin)) {
            box.className = "tie";
            box.textContent = "Tie";
            svg_elt.setAttribute('style', 'fill: #d3d3d3');
        } else {
            if (new_margin > 0) {
                box.textContent = "D+" + new_margin.toFixed(1);
                dem_evs += parseInt(tr.dataset.evs);
            } else if (new_margin < 0) {
                box.textContent = "R+" + Math.abs(new_margin).toFixed(1);
                gop_evs += parseInt(tr.dataset.evs);
            }
            var color = GetColorForMargin(new_margin);
            svg_elt.setAttribute('style', 'fill: ' + color[0]);
            box.className = color[1];
        }

        id++;
    }

    dem_ev_elt.textContent = dem_evs;
    gop_ev_elt.textContent = gop_evs;
    tie_ev_elt.textContent = total_evs - (dem_evs + gop_evs);
}
