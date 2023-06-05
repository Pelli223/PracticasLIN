#!/bin/bash

while true
do
   for (( i=0; $i<8 ; i++ ))
   do
      echo "add" $i > /proc/modlist
      sleep 0.3
   done
done
