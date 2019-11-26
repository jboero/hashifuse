# https://fedoraproject.org/wiki/How_to_create_an_RPM_package

Name:           vaultfs
Version:        0.1
Release:        1
Summary:        Hashicorp VaultFS Fuse Agent
License:        GPL+
Source0:        https://github.com/jboero/hashifuse/archive/master.zip
Requires(post): libcurl fuse jsoncpp
BuildRequires:  gcc-c++ libcurl-devel fuse-devel
BuildRequires:  jsoncpp-devel
URL:            https://www.vaultproject.io/

%define debug_package %{nil}

%description
FUSE filesystem for browsing and managing Hashicorp Vault secrets. Community project not supported by Hashicorp.

%prep
%autosetup -c %{name}-%{version}

%build
cd hashifuse-master/VaultFS
make

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p hashifuse-master/VaultFS/%{name} %{buildroot}%{_bindir}/

%clean
rm -rf %{buildroot}
rm -rf %{_builddir}/*

%files
%{_bindir}/%{name}

%pre
getent group %{name} > /dev/null || groupadd -r %{name}
getent passwd %{name} > /dev/null || \
    useradd -r -d %{_sharedstatedir}/%{name} -g %{name} \
    -s /sbin/nologin -c "Hashicorp vaultfs job scheduler" %{name}
exit 0

%post
%systemd_post %{name}.service
/sbin/setcap cap_ipc_lock=+ep %{_bindir}/%{name}

%preun
%systemd_preun %{name}.service

%postun
%systemd_postun_with_restart %{name}.service

%changelog

