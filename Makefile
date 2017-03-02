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

PKGNAMES = billmanager-plugin-$(PM_NAME)
RPM_PKGNAMES ?= $(PKGNAMES)

BASE ?= /usr/local/mgr5
include $(BASE)/src/isp.mk

localconfig: xml/billmgr_mod_$(PM_NAME).xml dist/etc/$(SHORT_NAME)_domainprice.json dist/etc/$(SHORT_NAME)_countries.json pkgs/rpm/specs/billmanager-plugin-$(PM_NAME).spec.in

xml/billmgr_mod_$(PM_NAME).xml: billmgr_mod.xml config.mk
	rm -f xml/billmgr_mod_* || true
	sed -e "s|__PM_NAME__|$(PM_NAME)|g" -e "s|__FULL_NAME__|$(FULL_NAME)|g" billmgr_mod.xml > xml/billmgr_mod_$(PM_NAME).xml

dist/etc/$(SHORT_NAME)_domainprice.json: config.mk
	rm -f dist/etc/*_domainprice.json || true
	wget -O dist/etc/$(SHORT_NAME)_domainprice.json "$(DOMAINPRICE_URL)"

dist/etc/$(SHORT_NAME)_countries.json: config.mk
	rm -f dist/etc/*_countries.json || true
	wget -O dist/etc/$(SHORT_NAME)_countries.json "$(COUNTRIES_URL)"

pkgs/rpm/specs/billmanager-plugin-$(PM_NAME).spec.in: config.mk rpm_spec.spec
	rm -f pkgs/rpm/specs/* || true
	sed -e "s|__PM_NAME__|$(PM_NAME)|g" -e "s|__SHORT_NAME__|$(SHORT_NAME)|g" -e "s|__RUTLD_PROD_URL__|$(RUTLD_PROD_URL)|g" rpm_spec.spec > pkgs/rpm/specs/billmanager-plugin-$(PM_NAME).spec.in
