SRC_NAME=dnsmasq-2.6x

all:
	$(MAKE) -C $(SRC_NAME) COPTS="$(if $(CONFIG_IPV6),-DUSE_IPV6,)"

clean:
	$(MAKE) -C $(SRC_NAME) clean
	rm -f dnsmasq

romfs:
	cp $(SRC_NAME)/src/dnsmasq .
	$(STRIP) dnsmasq
	$(ROMFSINST) /usr/sbin/dnsmasq

