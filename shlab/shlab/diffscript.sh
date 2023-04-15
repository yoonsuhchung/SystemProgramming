#!/bin/bash

for i in {1..16}
do
  make test$(printf "%02d" $i) > test$i.out
  make rtest$(printf "%02d" $i) > rtest$i.out
  diff test$i.out rtest$i.out
done
