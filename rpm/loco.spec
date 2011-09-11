Summary: An end-to-end bandwidth estimation tool.
Name: loco
Version: 0.4
Release: 1
License: GPL
Source0: https://github.com/downloads/firnsy/loco/loco-0.4.tar.gz
Source1: loco-0.4.tar.gz
URL: http://www.github.com/firnsy/loco.git
Packager: Ian Firns <firnsy@securixlive.com>

%description
An end-to-end bandwidth estimation tool.

%prep
%setup -q -a 0

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/local/bin
cp loco $RPM_BUILD_ROOT/usr/local/bin
cp locod $RPM_BUILD_ROOT/usr/local/bin

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0755,root,root)
/usr/local/bin/loco
/usr/local/bin/locod
