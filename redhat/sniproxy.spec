Name: sniproxy
Version: 0.3.4
Release: 1%{?dist}
Summary: Transparent TLS proxy

Group: System Environment/Daemons
License: BSD
URL: https://github.com/dlundquist/sniproxy
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires: autoconf, automake, curl, libev-devel, pcre-devel, perl, udns-devel

%description
Proxies incoming HTTP and TLS connections based on the hostname contained in
the initial request. This enables HTTPS name based virtual hosting to seperate
backend servers without the installing the private key on the proxy machine.


%prep
%setup -q


%build
%configure CFLAGS="-I/usr/include/libev"
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_sbindir}/sniproxy
%doc



%changelog
