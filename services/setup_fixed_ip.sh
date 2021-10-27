#! /bin/bash

cat "interface eth0" >> /etc/dhcpcd.conf
cat "static ip_address=192.168.1.1/24" >> /etc/dhcpcd.conf
cat "static routers=192.168.1.1" >> /etc/dhcpcd.conf
cat "static domain_name_servers=192.168.1.1" >> /etc/dhcpcd.conf
