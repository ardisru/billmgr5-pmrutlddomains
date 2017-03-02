%define core_dir /usr/local/mgr5

Name:                           billmanager-plugin-__PM_NAME__
Version:                        %%VERSION%%
Release:                        %%REL%%%{?dist}
Summary:                        billmanager-plugin-__PM_NAME__ package
Group:                          System Environment/Daemons
License:                        Commercial
URL:                            __RUTLD_PROD_URL__

BuildRequires: coremanager-devel%{?_isa} >= %%COREVERSION%%
BuildRequires: billmanager-corporate-devel >= %{version}-%{release}
Requires: coremanager
Requires: billmanager >= %{version}-%{release}

%description
billmanager-plugin-__PM_NAME__

%debug_package

%build
export LD_LIBRARY_PATH=".:./lib"
export CFLAGS="$RPM_OPT_FLAGS"
export CXXFLAGS="${CFLAGS}"
make localconfig
make %{?_smp_mflags} NOEXTERNAL=yes RELEASE=yes 

%install
export LD_LIBRARY_PATH=".:./lib"
export CFLAGS="$RPM_OPT_FLAGS"
export LDFLAGS="-L%{core_dir}/lib"
export CXXFLAGS="${CFLAGS}"
rm -rf $RPM_BUILD_ROOT
INSTALLDIR=%{buildroot}%{core_dir}
mkdir -p $INSTALLDIR
make %{?_smp_mflags} dist DISTDIR=$INSTALLDIR NOEXTERNAL=yes RELEASE=yes

%check

%clean
rm -rf $RPM_BUILD_ROOT

%post
. %{core_dir}/lib/pkgsh/core_pkg_funcs.sh
ReloadMgr billmgr

%postun
if [ $1 -eq 0 ]; then
. %{core_dir}/lib/pkgsh/core_pkg_funcs.sh
ReloadMgr billmgr
fi

%files
%defattr(-, root, root, -)
%{core_dir}/libexec/__PM_NAME__.so
%{core_dir}/processing/__PM_NAME__
%{core_dir}/etc/xml/billmgr_mod___PM_NAME__.xml
%{core_dir}/etc/__SHORT_NAME___countries.json
%{core_dir}/etc/__SHORT_NAME___domainprice.json
