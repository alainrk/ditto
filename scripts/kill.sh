#!/bin/sh

kill $(ps aux | grep 'bin/ditto' | head -1 | awk '{print $2}')
