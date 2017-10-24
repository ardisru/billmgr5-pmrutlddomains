include config.mk

MGR = billmgr
PLUGIN = $(PM_NAME)

CFLAGS += -I/usr/local/mgr5/include/billmgr
CXXFLAGS += -I/usr/local/mgr5/include/billmgr -DBINARY_NAME=\"$(PM_NAME)\" -DSHORT_NAME=\"$(SHORT_NAME)\" -DRUTLD_PROD_URL=\"$(RUTLD_PROD_URL)\" "-DRUTLD_PROJECT_NAME=\"$(RUTLD_PROJECT_NAME)\"" -DCLASS_NAME=$(CLASS_NAME)

WRAPPER += $(PM_NAME)
$(PM_NAME)_SOURCES = processing.cpp json11/json11.cpp
$(PM_NAME)_FOLDER = processing
$(PM_NAME)_LDADD = -lmgr -lmgrdb
$(PM_NAME)_DLIBS = processingmodule processingdomain

DOMAINPRICE_JSON = etc/$(SHORT_NAME)_domainprice.json
COUNTRIES_JSON = etc/$(SHORT_NAME)_countries.json
JSON = $(DOMAINPRICE_JSON) $(COUNTRIES_JSON)
DIST_XML = xml/$(PM_NAME).xml xml/$(PM_NAME)_msg_ru.xml xml/$(PM_NAME)_msg_en.xml

BASE ?= /usr/local/mgr5
include $(BASE)/src/isp.mk

.PHONY: install-json clean_json_xml dist_xml
.SUFFIXES: .xml

all: $(DIST_XML) $(JSON)

install: install-json

install-json: $(JSON)
	install -o root -g root -m 440 $(JSON) $(BASE)/etc/

xml-from-template: config.mk
	mkdir -p "$(shell dirname $(DST))"
	sed -e "s|__PM_NAME__|$(PM_NAME)|g" -e "s|__FULL_NAME__|$(FULL_NAME)|g" $(SRC) > $(DST)

download-json: config.mk
	mkdir -p "$(shell dirname $(FILE))"
	wget -O "$(FILE)" "$(URL)"

clean: clean_json_xml

clean_json_xml:
	$(RM) -r etc
	$(RM) -r xml

xml/$(PM_NAME).xml: mod.xml
	$(MAKE) xml-from-template SRC="$<" DST="$@"

xml/$(PM_NAME)%.xml: mod%.xml
	$(MAKE) xml-from-template SRC="$<" DST="$@"

$(DOMAINPRICE_JSON): config.mk
	$(MAKE) download-json FILE="$@" URL="$(DOMAINPRICE_URL)"

$(COUNTRIES_JSON): config.mk
	$(MAKE) download-json FILE="$@" URL="$(COUNTRIES_URL)"
