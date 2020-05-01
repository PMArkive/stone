# STONE

Simple Tracking Of National Elections

## About

This is a simple tool which can very quickly ingest and analyze polls for US elections. It outputs a single HTML dashboard that lists the polling status of every Senate, House, Gubernatorial, and statewide Presidential election. It also computes some topline predictions and summaries. However, the primary goal is to have a bare-bones but complete view of all races and their underlying data:

 - All races are on one page, providing a convenient dashboard.
 - The view is sorted to make it easy to identify critical or close races.
 - Races with new polls are highlighted so it's easy to see new polls or why an average changed.
 - Graphs are generated to show how races change over time.

## Model Status

We have support for poll scraping and ingesting the following elections:
 - Presidential elections 2004 onward.
 - Senate elections 2006 onward.
 - House elections (expert ratings only) 2006-2016.
 - House elections (expert ratings and polls) 2018 and onward.
 - Gubernatorial elections 2016 onward.

Data is ingested from a combination of FiveThirtyEight, RCP, and Wikipedia, depending on the year. However, candidate names have to be input manually. There are some ad-hoc Python scraping tools for this, but any mistakes have to be cured by hand.

## Wishlist

1. Smoother averages. HuffPollster or 538 have less "spiky" trendlines. Our average is a simple moving one, and when outliers age in or out, it creates large and sudden movements.
2. A real numeric probability. I don't particularly like probabilities (given these are one time, unreproducible events). But it would be nice to have. The current attempt is not very rigorous and is therefore converted to a rating instead.

## Requirements

AMBuild is needed.
Python 3.7 or higher is needed.
protobuf 3.6 or higher is needed.

	sudo apt-get install \
		python3-pip \
		libncurses5-dev \
		libssl-dev \
		protobuf-compiler \
		rapidjson-dev

	sudo pip3 install pyinstaller matplotlib

## Citations

This work is based on the ideas of the Princeton Election Consortium. In addition, we cite:

 - "A New Approach to Estimating the Probability of Winning the Presidency", Edward Kaplan and Arnold Barnett.
 - "Method for analysis of opinion polls in a national electoral system", Sam Wang PhD.
