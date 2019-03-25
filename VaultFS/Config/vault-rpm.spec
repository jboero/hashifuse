# https://fedoraproject.org/wiki/How_to_create_an_RPM_package

Name:           vaultfs
Version:        0.1
Release:        1%{?dist}
Summary:        Hashicorp VaultFS Fuse Agent
License:        Hashicorp
Source0:        vaultfs
Requires(post): libcurl fuse
URL:            https://www.vaultproject.io/

%define debug_package %{nil}

%description
Hahshicorp Vault secures, stores, and tightly controls access to tokens, passwords,
certificates, API keys, and other secrets in modern computing. vaultfs handles
leasing, key revocation, key rolling, and auditing. Through a unified API, users
can access an encrypted Key/Value store and network encryption-as-a-service, or
generate AWS IAM/STS credentials, SQL/NoSQL databases, X.509 certificates, SSH
credentials, and more.

%prep
%autosetup -c %{name}-%{version}

%build

%install

mkdir -p %{buildroot}%{_bindir}/
cp -p %{name} %{buildroot}%{_bindir}/

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
