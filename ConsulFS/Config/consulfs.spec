Name:           consulfs
Version:        0.2
Release:        1
Summary:        Hashicorp Kubernetes Fuse Agent
License:        GPL+
Source0:        https://github.com/jboero/hashifuse/archive/master.zip
Requires(post): libcurl fuse jsoncpp
BuildRequires:  gcc-c++ libcurl-devel fuse-devel jsoncpp-devel
URL:            https://github.com/jboero/hashifuse

%description
FUSE filesystem for browsing and managing Consul resources as files. Community project not supported by Hashicorp.

%prep
%autosetup -c %{name}-%{version}

%build
cd hashifuse-master/ConsulFS
g++ -g -o %{name} $CFLAGS -D_FILE_OFFSET_BITS=64 -std=c++11 -lfuse -ljsoncpp -lcurl main.cpp

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p hashifuse-master/ConsulFS/%{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%changelog
