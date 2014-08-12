#!/bin/bash

#itsepurk.sh nimi sailytysjannite ah
#itsepurk.sh 26H 4.20 2.6

testijannite="4.20"
purkujannite="2.50"
sailytysjannite=$2
Cper40=`echo "scale=5;"${3}"/40" | bc`
Cper20=`echo "scale=5;"${3}"/20" | bc`
Cper10=`echo "scale=5;"${3}"/10" | bc`
Cper5=`echo "scale=5;"${3}"/5" | bc`
Cper2=`echo "scale=5;"${3}"/2" | bc`
voltname=`echo $2 | sed s/\\\.//`

set -x
./battest ${1}_${voltname}_alkulat c $Cper2 $testijannite $Cper20
sleep 60

./battest ${1}_${voltname}_sykli1d dr $Cper2 $purkujannite $Cper5
sleep 60
./battest ${1}_${voltname}_sykli1c cr $Cper2 $testijannite $Cper20
sleep 60
./battest ${1}_${voltname}_sykli2d dr $Cper2 $purkujannite $Cper5
sleep 60
./battest ${1}_${voltname}_sykli2c cr $Cper2 $testijannite $Cper20
sleep 60
./battest ${1}_${voltname}_sykli3d dr $Cper2 $purkujannite $Cper5
sleep 60

./battest ${1}_${voltname}_varastolat1 c $Cper2 $sailytysjannite $Cper5
sleep 300
echo "Paina enter ja poista kenno heti latauksen valmistuttua."
read
./battest ${1}_${voltname}_varastolat1 c $Cper5 $sailytysjannite $Cper40
