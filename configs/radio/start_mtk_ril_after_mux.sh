#!/system/bin/sh

sleep 3

if [ -e /dev/radio/atci1 ]; then
  (
    exec 3<> /dev/radio/atci1
    printf 'AT+EFUN=1\r' >&3
    sleep 1
  ) >/dev/null 2>&1
fi

setprop vendor.ril.mtk 1
