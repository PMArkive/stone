// vim: sts=4 ts=8 sw=4 tw=99 et:
'use strict';

function ExpandPolls(span, block_id)
{
    var poll_block = document.getElementById(block_id);
    if (span.xlink_on) {
        span.classList.remove('rotate')
        span.xlink_on = false;
        poll_block.classList.add('invisible');
    } else {
        span.classList.add('rotate')
        span.xlink_on = true;
        poll_block.classList.remove('invisible');
    }
}

var kFractions = [
    [1, 10], [1, 10], [1, 10], [1, 10], [1, 10], [1, 10], [1, 10], [1, 10], [1, 10],
    [1, 10], [1, 10], [1, 9], [1, 8], [1, 8], [1, 7], [1, 7], [1, 6], [1, 6], [1, 6],
    [1, 5], [1, 5], [1, 5], [2, 9], [2, 9], [1, 4], [1, 4], [1, 4], [2, 7], [2, 7],
    [2, 7], [3, 10], [3, 10], [1, 3], [1, 3], [1, 3], [1, 3], [3, 8], [3, 8], [3, 8],
    [2, 5], [2, 5], [2, 5], [3, 7], [3, 7], [4, 9], [4, 9], [4, 9], [4, 9], [1, 2],
    [1, 2], [1, 2], [1, 2], [1, 2], [5, 9], [5, 9], [5, 9], [5, 9], [4, 7], [4, 7],
    [3, 5], [3, 5], [3, 5], [5, 8], [5, 8], [5, 8], [2, 3], [2, 3], [2, 3], [2, 3],
    [7, 10], [7, 10], [5, 7], [5, 7], [5, 7], [3, 4], [3, 4], [3, 4], [7, 9], [7, 9],
    [4, 5], [4, 5], [4, 5], [5, 6], [5, 6], [5, 6], [6, 7], [6, 7], [7, 8], [7, 8],
    [8, 9], [9, 10], [9, 10], [9, 10], [9, 10], [9, 10], [9, 10], [9, 10], [9, 10],
    [9, 10], [9, 10], [9, 10]
];


function TogglePercent(span, pct_value)
{
    if (!span.x_showing_pct)
        span.x_showing_pct = "rating";

    if (span.x_showing_pct == "rating") {
        span.x_old_content = span.textContent;

        var prefix = "Even";
        if (pct_value < 50.0) {
            prefix = "R";
            pct_value = 100.0 - pct_value; // Flip since pct_value is P(dem).
        } else if (pct_value > 50.0) {
            prefix = "D";
        }

        // Skip showing fractions for tossups.
        if (prefix == "Even")
            span.x_showing_pct = "fraction";
        else
            span.x_showing_pct = "pct";
        span.textContent = prefix + " " + parseInt(pct_value) + '%';
    } else if (span.x_showing_pct == "pct") {
        var prefix;
        if (pct_value < 50.0) {
            prefix = "R";
            pct_value = 100.0 - pct_value; // Flip since pct_value is P(dem).
        } else if (pct_value > 50.0) {
            prefix = "D";
        }

        var parts = kFractions[pct_value];
        span.textContent = prefix + " " + parts[0] + " in " + parts[1];
        span.x_showing_pct = "fraction";
    } else if (span.x_showing_pct == "fraction") {
        span.textContent = span.x_old_content;
        span.x_showing_pct = "rating";
    }
}

function AddRangeTicks(div) {
    var ticks = [-10, -8, -6, -4, -2, 0,
                   2,  4,  6,  8, 10];
    ticks.forEach(function (item, index) {
        var span = document.createElement('span');
        if (item > 0) {
            span.textContent = "D+" + item;
        } else if (item < 0) {
            span.textContent = "R+" + Math.abs(item);
        } else {
            span.textContent = "0";
        }
        span.setAttribute('class', 'tick');
        div.appendChild(span);
    });
}
