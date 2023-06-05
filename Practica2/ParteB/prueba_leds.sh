#!/bin/bash

while true
do
   for (( i=0; $i<8 ; i++ ))
   do
      sudo ./ledct1_invoke $i
      sleep 0.5
    done
done
