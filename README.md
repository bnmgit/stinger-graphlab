stinger-graphlab
================

A collaboration / connection between STINGER and GraphLab

To run
======

Start the server either in the background with

  ./server &

or in another terminal session.  Then run

  ./client_demo -i 30 -o 3 -j path/to/twitter.json

This will start streaming data in Twitter JSON format (raw captured
off of the [twitter sample
stream](https://stream.twitter.com/1.1/statuses/sample.json) into
STINGER.  The parameters -i indicate input time span per batch in
seconds and -o output time between batches (so this will replay 30
seconds of Twitter capture in 3 seconds time or at 10x speed).

When the server is running, you can open a web browser to http://
localhost:8088/html to open a tool that will allow you to explore
subgraphs within the full STINGER dataset.  Try searching for YouTube.
If nothing is displayed, no tweets mentioning @YouTube have occurred
yet.
