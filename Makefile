include config.mk

BASE ?= /usr/local/mgr5
MGR = billmgr
PLUGIN = $(PM_NAME)

CFLAGS += -I$(BASE)/include/billmgr
CXXFLAGS += -I$(BASE)/include/billmgr

WRAPPER += $(PM_NAME)
$(PM_NAME)_SOURCES = processing.cpp json11/json11.cpp
$(PM_NAME)_HEADERS = config.h
$(PM_NAME)_FOLDER = processing
$(PM_NAME)_LDADD = -lmgr -lmgrdb
$(PM_NAME)_DLIBS = processingmodule processingdomain

DOMAINPRICE_JSON = etc/$(SHORT_NAME)_domainprice.json
COUNTRIES_JSON = etc/$(SHORT_NAME)_countries.json
JSON = $(DOMAINPRICE_JSON) $(COUNTRIES_JSON)
DIST_XML = xml/$(PM_NAME).xml xml/$(PM_NAME)_msg_ru.xml xml/$(PM_NAME)_msg_en.xml
CONFIG = config.mk

include $(BASE)/src/isp.mk

.PHONY: install-json clean_json_xml dist_xml
.SUFFIXES: .xml

all: $(JSON)

install: install-json

install-json: $(JSON)
	install -o root -g root -m 440 $(JSON) $(BASE)/etc/

xml-from-template: $(CONFIG)
	mkdir -p "$(shell dirname $(DST))"
	sed -e "s|__PM_NAME__|$(PM_NAME)|g" -e "s|__FULL_NAME__|$(FULL_NAME)|g" $(SRC) > $(DST)

download-json: $(CONFIG)
	mkdir -p "$(shell dirname $(FILE))"
	wget -O "$(FILE)" "$(URL)"

clean: clean-generated

clean-generated:
	$(RM) -r etc
	$(RM) -r xml
	$(RM) config.h

processing.cpp: config.h $(DIST_XML)

config.h: config.h.in $(CONFIG)
	sed -e "s|__BINARY_NAME__|$(PM_NAME)|g" \
		-e "s|__SHORT_NAME__|$(SHORT_NAME)|g" \
		-e "s|__RUTLD_PROD_URL__|$(RUTLD_PROD_URL)|g" \
		-e "s|__RUTLD_PROJECT_NAME__|$(RUTLD_PROJECT_NAME)|g" \
		-e "s|__CLASS_NAME__|$(CLASS_NAME)|g" \
		config.h.in > "$@"

xml/$(PM_NAME).xml: mod.xml $(CONFIG)
	$(MAKE) xml-from-template SRC="$<" DST="$@"

xml/$(PM_NAME)%.xml: mod%.xml $(CONFIG)
	$(MAKE) xml-from-template SRC="$<" DST="$@"

$(DOMAINPRICE_JSON): $(CONFIG)
	$(MAKE) download-json FILE="$@" URL="$(DOMAINPRICE_URL)"

$(COUNTRIES_JSON): $(CONFIG)
	$(MAKE) download-json FILE="$@" URL="$(COUNTRIES_URL)"
