#!/bin/bash

while true
do
   for (( i=0; $i<8 ; i++ ))
   do
      echo $i > /dev/leds
      sleep 0.4
   done
done
