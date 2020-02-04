Name:           vaultfs
Version:        0.1
Release:        1
Summary:        Hashicorp VaultFS Fuse Agent
License:        GPL+
Source0:        https://github.com/jboero/hashifuse/archive/master.zip
Requires(post): libcurl fuse jsoncpp
BuildRequires:  gcc-c++ libcurl-devel fuse-devel jsoncpp-devel
URL:            https://www.vaultproject.io/

%description 
FUSE filesystem for browsing and managing Hashicorp Vault secrets. Community project not supported by Hashicorp.

%prep
%autosetup -c %{name}-%{version}

%build
cd hashifuse-master/VaultFS
g++ -o vault $CFLAGS -D_FILE_OFFSET_BITS=64 -O3 -std=c++11 -lfuse -ljsoncpp -lcurl main.cpp

%install
mkdir -p %{buildroot}%{_bindir}
cp -p hashifuse-master/VaultFS/%{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%changelog

