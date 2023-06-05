#!/bin/bash

for (( i=0; $i<20 ; i++ ))
   do
      echo $i;
      echo $i > /dev/prodcons
done
