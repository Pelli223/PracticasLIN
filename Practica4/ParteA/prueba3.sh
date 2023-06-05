#!/bin/bash

while true
do
   for (( i=0; $i<8 ; i++ ))
   do
      echo "remove" $i > /proc/modlist
      sleep 0.6
   done
done
